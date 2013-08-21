#include "IndexSearchService.h"

#include <node-manager/MasterManagerBase.h>
#include <aggregator-manager/SearchMerger.h>
#include <aggregator-manager/SearchWorker.h>

#include <common/SearchCache.h>
#include <common/SFLogger.h>
#include <common/type_defs.h>

namespace sf1r
{

IndexSearchService::IndexSearchService(IndexBundleConfiguration* config)
    : bundleConfig_(config)
    , searchMerger_(NULL)
    , searchCache_(new SearchCache(bundleConfig_->masterSearchCacheNum_,
                                    bundleConfig_->refreshCacheInterval_,
                                    bundleConfig_->refreshSearchCache_))
{
}

IndexSearchService::~IndexSearchService()
{
}

boost::shared_ptr<SearchAggregator> IndexSearchService::getSearchAggregator()
{
    return searchAggregator_;
}

const IndexBundleConfiguration* IndexSearchService::getBundleConfig()
{
    return bundleConfig_;
}

void IndexSearchService::OnUpdateSearchCache()
{
    searchCache_->clear();
}

bool IndexSearchService::getSearchResult(
    KeywordSearchActionItem& actionItem,
    KeywordSearchResult& resultItem
)
{
    CREATE_SCOPED_PROFILER (query, "IndexSearchService", "processGetSearchResults all: total query time");

    LOG(INFO) << "Search Begin." << endl;
    if (!MasterManagerBase::get()->isDistributed() || !searchAggregator_->isNeedDistribute())
    {
        bool ret = searchWorker_->doLocalSearch(actionItem, resultItem);
        net::aggregator::WorkerResults<KeywordSearchResult> workerResults;
        workerResults.add(0, resultItem);
        searchMerger_->getMiningResult(workerResults, resultItem);
        LOG(INFO) << "Local Search End." << endl;
        return ret;
    }

    /// Perform distributed search by aggregator
    KeywordSearchResult distResultItem;
    distResultItem.distSearchInfo_.isDistributed_ = true;
    distResultItem.distSearchInfo_.effective_ = true;
    distResultItem.distSearchInfo_.nodeType_ = DistKeywordSearchInfo::NODE_WORKER;

    if (actionItem.searchingMode_.mode_ == SearchingMode::WAND)
    {
        distResultItem.distSearchInfo_.option_ = DistKeywordSearchInfo::OPTION_GATHER_INFO;
        searchAggregator_->distributeRequest<KeywordSearchActionItem, DistKeywordSearchInfo>(
            actionItem.collectionName_, "getDistSearchInfo", actionItem, distResultItem.distSearchInfo_);

        distResultItem.distSearchInfo_.option_ = DistKeywordSearchInfo::OPTION_CARRIED_INFO;
    }

    typedef std::map<workerid_t, KeywordSearchResult> ResultMapT;
    typedef ResultMapT::iterator ResultMapIterT;

    QueryIdentity identity;
    // For distributed search, as it should merge the results over all nodes,
    // the topK start offset is fixed to zero
    size_t topKStart = actionItem.pageInfo_.topKStart(bundleConfig_->topKNum_, IsTopKComesFromConfig(actionItem));
    LOG(INFO) << "topKStart for dist search is " << topKStart << ", pageInfo_ :"
        << actionItem.pageInfo_.start_ << ", " << actionItem.pageInfo_.count_;
    searchWorker_->makeQueryIdentity(identity, actionItem, distResultItem.distSearchInfo_.option_, topKStart);

    if (!searchCache_->get(identity, resultItem))
    {
        // Get and aggregate keyword search results from mutliple nodes
        distResultItem.setStartCount(actionItem.pageInfo_);

        searchAggregator_->distributeRequest(
                actionItem.collectionName_, "getDistSearchResult", actionItem, distResultItem);
        // remove the first topKStart docids.
        if (topKStart > 0)
        {
            if( !distResultItem.topKDocs_.empty() )
            {
                size_t erase_to = std::min(topKStart, distResultItem.topKDocs_.size());
                distResultItem.topKDocs_.erase(distResultItem.topKDocs_.begin(),
                    distResultItem.topKDocs_.begin() + erase_to);
            }
            if( !distResultItem.topKRankScoreList_.empty() )
            {
                size_t erase_to = std::min(topKStart, distResultItem.topKRankScoreList_.size());
                distResultItem.topKRankScoreList_.erase(distResultItem.topKRankScoreList_.begin(),
                    distResultItem.topKRankScoreList_.begin() + erase_to);
            }
            if (!distResultItem.topKCustomRankScoreList_.empty())
            {
                size_t erase_to = std::min(topKStart, distResultItem.topKCustomRankScoreList_.size());
                distResultItem.topKCustomRankScoreList_.erase(distResultItem.topKCustomRankScoreList_.begin(),
                    distResultItem.topKCustomRankScoreList_.begin() + erase_to);
            }
        }

        distResultItem.adjustStartCount(topKStart);

        resultItem.swap(distResultItem);
        resultItem.distSearchInfo_.nodeType_ = DistKeywordSearchInfo::NODE_MASTER;

        searchWorker_->rerank(actionItem, resultItem);

        // Get and aggregate Summary, Mining results from multiple nodes.
        ResultMapT resultMap;
        searchMerger_->splitSearchResultByWorkerid(resultItem, resultMap);
        if (resultMap.empty())
        {
            // empty is meaning we do not need send request to any worker to get 
            // any documents. But we do need to get mining result.
            LOG(INFO) << "empty worker map after split. get mining result from all workers";
            //KeywordSearchResult workerResult;
            //workerResult.propertyQueryTermList_ = resultItem.propertyQueryTermList_;
            //workerResult.rawQueryString_ = resultItem.rawQueryString_;
            //workerResult.pruneQueryString_ = resultItem.pruneQueryString_;
            //workerResult.encodingType_ = resultItem.encodingType_;
            //workerResult.collectionName_ = resultItem.collectionName_;
            //workerResult.analyzedQuery_ = resultItem.analyzedQuery_;
            //workerResult.queryTermIdList_ = resultItem.queryTermIdList_;
            //workerResult.totalCount_ = resultItem.totalCount_;
            //workerResult.TOP_K_NUM = resultItem.TOP_K_NUM;
            //workerResult.distSearchInfo_.isDistributed_ = resultItem.distSearchInfo_.isDistributed_;
            //searchAggregator_->distributeRequest(actionItem.collectionName_, "getSummaryMiningResult",
            //    actionItem, resultItem);
        }
        else
        {
            RequestGroup<KeywordSearchActionItem, KeywordSearchResult> requestGroup;
            for (ResultMapIterT it = resultMap.begin(); it != resultMap.end(); it++)
            {
                workerid_t workerid = it->first;
                KeywordSearchResult& subResultItem = it->second;
                requestGroup.addRequest(workerid, &actionItem, &subResultItem);
            }

            searchAggregator_->distributeRequest(
                actionItem.collectionName_, "getSummaryMiningResult", requestGroup, resultItem);
        }
        searchCache_->set(identity, resultItem);
    }
    else
    {
        resultItem.setStartCount(actionItem.pageInfo_);
        resultItem.adjustStartCount(topKStart);

        ResultMapT resultMap;
        searchMerger_->splitSearchResultByWorkerid(resultItem, resultMap);
        RequestGroup<KeywordSearchActionItem, KeywordSearchResult> requestGroup;
        for (ResultMapIterT it = resultMap.begin(); it != resultMap.end(); it++)
        {
            workerid_t workerid = it->first;
            KeywordSearchResult& subResultItem = it->second;
            requestGroup.addRequest(workerid, &actionItem, &subResultItem);
        }

        searchAggregator_->distributeRequest(
                actionItem.collectionName_, "getSummaryResult", requestGroup, resultItem);
    }

    cout << "Total count: " << resultItem.totalCount_ << endl;
    cout << "Top K count: " << resultItem.topKDocs_.size() << endl;
    cout << "Page Count: " << resultItem.count_ << endl;
    LOG(INFO) << "Search Finished " << endl;

    REPORT_PROFILE_TO_FILE( "PerformanceQueryResult.SIAProcess" );

    return true;
}

bool IndexSearchService::getDocumentsByIds(
    const GetDocumentsByIdsActionItem& actionItem,
    RawTextResultFromSIA& resultItem
)
{
    if (!MasterManagerBase::get()->isDistributed() || !searchAggregator_->isNeedDistribute())
    {
        searchWorker_->getDocumentsByIds(actionItem, resultItem);
        return !resultItem.idList_.empty();
    }
    /// Perform distributed search by aggregator
    typedef std::map<workerid_t, GetDocumentsByIdsActionItem> ActionItemMapT;
    typedef ActionItemMapT::iterator ActionItemMapIterT;

    ActionItemMapT actionItemMap;
    if (!searchMerger_->splitGetDocsActionItemByWorkerid(actionItem, actionItemMap))
    {
        searchAggregator_->distributeRequest(actionItem.collectionName_, "getDocumentsByIds", actionItem, resultItem);
    }
    else
    {
        RequestGroup<GetDocumentsByIdsActionItem, RawTextResultFromSIA> requestGroup;
        for (ActionItemMapIterT it = actionItemMap.begin(); it != actionItemMap.end(); it++)
        {
            workerid_t workerid = it->first;
            GetDocumentsByIdsActionItem& subActionItem = it->second;
            requestGroup.addRequest(workerid, &subActionItem);
        }

        searchAggregator_->distributeRequest(actionItem.collectionName_, "getDocumentsByIds", requestGroup, resultItem);
    }

    return !resultItem.idList_.empty();
}

bool IndexSearchService::getInternalDocumentId(
    const std::string& collectionName,
    const uint128_t& scdDocumentId,
    uint64_t& internalId
)
{
    internalId = 0;
    if (!MasterManagerBase::get()->isDistributed() || !searchAggregator_->isNeedDistribute())
    {
        searchWorker_->getInternalDocumentId(scdDocumentId, internalId);
        internalId = net::aggregator::Util::GetWDocId(searchAggregator_->getLocalWorker(), (uint32_t)internalId);
    }
    else
    {
        searchAggregator_->distributeRequest<uint128_t, uint64_t>(
                collectionName, "getInternalDocumentId", scdDocumentId, internalId);
    }

    return (internalId != 0);
}

uint32_t IndexSearchService::getDocNum(const std::string& collection)
{
    if (!MasterManagerBase::get()->isDistributed() || !searchAggregator_->isNeedDistribute())
        return searchWorker_->getDocNum();
    else
    {
        uint32_t total_docs = 0;
        searchAggregator_->distributeRequest(collection, "getDistDocNum", total_docs);
        return total_docs;
    }
}

uint32_t IndexSearchService::getKeyCount(const std::string& collection, const std::string& property_name)
{
    if (!MasterManagerBase::get()->isDistributed() || !searchAggregator_->isNeedDistribute())
        return searchWorker_->getKeyCount(property_name);
    else
    {
        uint32_t total_docs = 0;
        searchAggregator_->distributeRequest(collection, "getDistKeyCount", property_name, total_docs);
        return total_docs;
    }
}

}
