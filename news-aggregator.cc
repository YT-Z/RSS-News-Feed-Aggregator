/**
 * File: news-aggregator.cc
 * --------------------------------
 * Presents the implementation of the NewsAggregator class.
 */
#include <map>
#include <set>

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


/**
 * Private Constructor: NewsAggregator
 * -----------------------------------
 * Self-explanatory.
 */
static const size_t kNumFeedWorkers = 8;
static const size_t kNumArticleWorkers = 64;
NewsAggregator::NewsAggregator(const string& rssFeedListURI, bool verbose): 
  log(verbose), rssFeedListURI(rssFeedListURI), built(false), feedPool(kNumFeedWorkers), articlePool(kNumArticleWorkers) {}


void NewsAggregator::processSingleArticle(const Article& a) {
  const title& articleTitle = a.title;
  const url& articleUrl = a.url;

  const server& articleServer= getURLServer(articleUrl);
  ServerAndTitle p_server_title = ServerAndTitle(articleServer, articleTitle);

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

  articlesTokensMutex.lock();
  if (tokensMap.find(p_server_title) != tokensMap.end()) {
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
  articlesTokensMutex.unlock();
}


void NewsAggregator::processSingleFeed(const pair<url, title>& f) {
  const url& curr_url = f.first;
  const title& curr_title = f.second;
  
  visitedURLsMutex.lock();
  if (visitedURLs.find(curr_url) != visitedURLs.end()) {
    // duplicate url
    log.noteSingleFeedDownloadSkipped(curr_url);
    visitedURLsMutex.unlock();
    return; 
  } 
  visitedURLs.insert(curr_url);
  visitedURLsMutex.unlock();

  RSSFeed feed(curr_url);
  // store articles in feed
  try {
    log.noteSingleFeedDownloadBeginning(curr_url);
    feed.parse();
  } catch (const RSSFeedException& rfe) {
    log.noteSingleFeedDownloadFailure(curr_url);
    return;
  }
  log.noteSingleFeedDownloadEnd(curr_url);

  // get articles
  const vector<Article>& articles = feed.getArticles();

  // for each article, store tokens in html-document
  for (const Article& a : articles) {
    articlePool.schedule([this, a]{
      processSingleArticle(a);
    });
  }
  log.noteAllArticlesHaveBeenScheduled(curr_title);
  articlePool.wait();
}

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
  log.noteFullRSSFeedListDownloadEnd();

  // get url-title map
  const map<url, title>& feeds = feedList.getFeeds();

  for (const pair<url, title>& f : feeds) {
    feedPool.schedule([this, f]{
      processSingleFeed(f);
    });
  }
  log.noteAllRSSFeedsDownloadEnd();

  feedPool.wait();
  
  // add to index
  for (const auto& t : tokensMap) {
    index.add(articlesMap[t.first], tokensMap[t.first]);
  }

 
}
