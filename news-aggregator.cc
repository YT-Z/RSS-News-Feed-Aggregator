/**
 * File: news-aggregator.cc
 * --------------------------------
 * Presents the implementation of the NewsAggregator class.
 */
#include <unordered_map>
#include <unordered_set>

#include "news-aggregator.h"
#include <iostream>
#include <iomanip>
#include <memory>
#include <thread>
#include <iostream>
#include <algorithm>
#include <thread>
#include <utility>

#include <getopt.h>
#include <libxml/parser.h>
#include <libxml/catalog.h>
#include "rss-feed.h"
#include "rss-feed-list.h"
#include "html-document.h"
#include "html-document-exception.h"
#include "rss-feed-exception.h"
#include "rss-feed-list-exception.h"
#include "utils.h"
#include "ostreamlock.h"
#include "string-utils.h"
using namespace std;

/**
 * Factory Method: createNewsAggregator
 * ------------------------------------
 * Factory method that spends most of its energy parsing the argument vector
 * to decide what RSS feed list to process and whether to print lots of
 * of logging information as it does so.
 */
static const string kDefaultRSSFeedListURL = "small-feed.xml";
NewsAggregator *NewsAggregator::createNewsAggregator(int argc, char *argv[]) {
  struct option options[] = {
    {"verbose", no_argument, NULL, 'v'},
    {"quiet", no_argument, NULL, 'q'},
    {"url", required_argument, NULL, 'u'},
    {NULL, 0, NULL, 0},
  };
  
  string rssFeedListURI = kDefaultRSSFeedListURL;
  bool verbose = true;
  while (true) {
    int ch = getopt_long(argc, argv, "vqu:", options, NULL);
    if (ch == -1) break;
    switch (ch) {
    case 'v':
      verbose = true;
      break;
    case 'q':
      verbose = false;
      break;
    case 'u':
      rssFeedListURI = optarg;
      break;
    default:
      NewsAggregatorLog::printUsage("Unrecognized flag.", argv[0]);
    }
  }
  
  argc -= optind;
  if (argc > 0) NewsAggregatorLog::printUsage("Too many arguments.", argv[0]);
  return new NewsAggregator(rssFeedListURI, verbose);
}

/**
 * Method: buildIndex
 * ------------------
 * Initalizex the XML parser, processes all feeds, and then
 * cleans up the parser.  The lion's share of the work is passed
 * on to processAllFeeds, which you will need to implement.
 */
void NewsAggregator::buildIndex() {
  if (built) return;
  built = true; // optimistically assume it'll all work out
  xmlInitParser();
  xmlInitializeCatalog();
  processAllFeeds();
  xmlCatalogCleanup();
  xmlCleanupParser();
}

/**
 * Method: queryIndex
 * ------------------
 * Interacts with the user via a custom command line, allowing
 * the user to surface all of the news articles that contains a particular
 * search term.
 */
void NewsAggregator::queryIndex() const {
  static const size_t kMaxMatchesToShow = 15;
  while (true) {
    cout << "Enter a search term [or just hit <enter> to quit]: ";
    string response;
    getline(cin, response);
    response = trim(response);
    if (response.empty()) break;
    const vector<pair<Article, int> >& matches = index.getMatchingArticles(response);
    if (matches.empty()) {
      cout << "Ah, we didn't find the term \"" << response << "\". Try again." << endl;
    } else {
      cout << "That term appears in " << matches.size() << " article"
           << (matches.size() == 1 ? "" : "s") << ".  ";
      if (matches.size() > kMaxMatchesToShow)
        cout << "Here are the top " << kMaxMatchesToShow << " of them:" << endl;
      else if (matches.size() > 1)
        cout << "Here they are:" << endl;
      else
        cout << "Here it is:" << endl;
      size_t count = 0;
      for (const pair<Article, int>& match: matches) {
        if (count == kMaxMatchesToShow) break;
        count++;
        string title = match.first.title;
        if (shouldTruncate(title)) title = truncate(title);
        string url = match.first.url;
        if (shouldTruncate(url)) url = truncate(url);
        string times = match.second == 1 ? "time" : "times";
        cout << "  " << setw(2) << setfill(' ') << count << ".) "
             << "\"" << title << "\" [appears " << match.second << " " << times << "]." << endl;
        cout << "       \"" << url << "\"" << endl;
      }
    }
  }
}

struct pair_hash {
  template<class T1, class T2>
  std::size_t operator () (const std:: pair<T1, T2> &p) const {
    auto h1 = std::hash<T1>{}(p.first);
    auto h2 = std::hash<T2>{}(p.second);
    return h1^h2;
  }
};
/**
 * Private Constructor: NewsAggregator
 * -----------------------------------
 * Self-explanatory.
 */
static const size_t kNumFeedWorkers = 8;
static const size_t kNumArticleWorkers = 64;
NewsAggregator::NewsAggregator(const string& rssFeedListURI, bool verbose): 
  log(verbose), rssFeedListURI(rssFeedListURI), built(false), feedPool(kNumFeedWorkers), articlePool(kNumArticleWorkers) {}

/**
 * Private Method: processAllFeeds
 * -------------------------------
 * The provided code (commented out, but it compiles) illustrates how one can
 * programmatically drill down through an RSSFeedList to arrive at a collection
 * of RSSFeeds, each of which can be used to fetch the series of articles in that feed.
 *
 * You'll want to erase much of the code below and ultimately replace it with
 * your multithreaded aggregator.
 */
void NewsAggregator::processAllFeeds() {

  RSSFeedList feedList(rssFeedListURI);

  // parse to store url-title map (feeds) in feedList
  try {
    feedList.parse();
  } catch (const RSSFeedListException& rfle) {
    log.noteFullRSSFeedListDownloadFailureAndExit(rssFeedListURI);
  }

  // get url-title map
  const map<string, string>& feeds = feedList.getFeeds();

  // for each url in feedlist, store articles of it in RSSfeed, store tokens in html-document
  unordered_set<string> visitedURLs;
  // map< pair<serverAndTitle>,  vector<string> tokens>
  unordered_map<pair<string, string>, vector<string>, pair_hash> tokensMap; 
  unordered_map<pair<string, string>, Article, pair_hash> articlesMap;

  for (const pair<string, string>& f : feeds) {
    const string& url = f.first;

    if (visitedURLs.find(url) != visitedURLs.end()) continue; // duplicate url
    visitedURLs.insert(url);

    RSSFeed feed(url);
    // store articles in feed
    try {
      feed.parse();
    } catch (const RSSFeedException& rfe) {
      log.noteSingleFeedDownloadFailure(url);
      return;
    }

    // get articles
    const vector<Article>& articles = feed.getArticles();

    // for each article, store tokens in html-document
    for (const Article& a : articles) {
      const string& articleTitle = a.title;
      const string& articleUrl = a.url;

      const string& server = getURLServer(articleUrl);
      string articleServer = server;
      pair<string, string> p_server_title = make_pair(articleServer, articleTitle);

      HTMLDocument document(articleUrl);
      try {
        document.parse();
      } catch (const HTMLDocumentException& hde) {
        log.noteSingleArticleDownloadFailure(a);
        return;
      }

      // get tokens
      const vector<string>& tokens = document.getTokens();
      vector<string> currTokens = tokens;
      sort(currTokens.begin(), currTokens.end());

      unordered_map<pair<string, string>, vector<string>, pair_hash>::iterator it = tokensMap.find(p_server_title);
      if (it != tokensMap.end()) {
        // article with same domain and title
        string& preUrl = articlesMap[p_server_title].url;
        const string& currUrl = document.getURL();

        // get intersected tokens
        vector<string>& preTokens = tokensMap[p_server_title];
        vector<string> intersectTokens;
        set_intersection(preTokens.cbegin(), preTokens.cend(), currTokens.cbegin(), currTokens.cend(), back_inserter(intersectTokens));
  
        if (preUrl.compare(currUrl) < 0) {
          // update tokensMap and articlesMap of preUrl
          tokensMap[p_server_title] = intersectTokens;
        } else {
          // update tokensMap and articlesMap currUrl, replace with preUrl
          tokensMap[p_server_title] = intersectTokens;
          articlesMap[p_server_title] = a;
        }
      } else {
        // unique article
        tokensMap[p_server_title] = currTokens;
        articlesMap[p_server_title] = a;
      }
    }
  }
  
  // add to index
  for (auto& t : tokensMap) {
    pair<string, string> serverAndTitle = t.first;
    index.add(articlesMap[serverAndTitle], tokensMap[serverAndTitle]);
  }

 
}
