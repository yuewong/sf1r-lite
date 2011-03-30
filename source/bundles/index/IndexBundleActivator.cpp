#include "IndexBundleActivator.h"

#include <common/SFLogger.h>
#include <la-manager/LAPool.h>

#include <util/singleton.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <memory> // for auto_ptr
#include <protect/RestrictMacro.h>

namespace bfs = boost::filesystem;
using namespace izenelib::util;

namespace sf1r
{

using namespace izenelib::osgi;
IndexBundleActivator::IndexBundleActivator()
    :tracker_(0)
    ,context_(0)
    ,searchService_(0)
    ,searchServiceReg_(0)
    ,taskService_(0)
    ,taskServiceReg_(0)
    ,config_(0)
{
}

IndexBundleActivator::~IndexBundleActivator()
{
}

void IndexBundleActivator::start( IBundleContext::ConstPtr context )
{
    context_ = context;

    boost::shared_ptr<BundleConfiguration> bundleConfigPtr = context->getBundleConfig();
    config_ = static_cast<IndexBundleConfiguration*>(bundleConfigPtr.get());
    init_();

    Properties props;
    props.put( "instance", "1" );
    searchServiceReg_ = context->registerService( "IndexSearchService", searchService_, props );
    taskServiceReg_ = context->registerService( "TaskSearchService", taskService_, props );
}

void IndexBundleActivator::stop( IBundleContext::ConstPtr context )
{
    searchServiceReg_->unregister();
    taskServiceReg_->unregister();
    delete searchService_;
    delete taskService_;
    delete searchServiceReg_;
    delete taskServiceReg_;
}

bool IndexBundleActivator::addingService( const ServiceReference& ref )
{
    return true;
}

void IndexBundleActivator::removedService( const ServiceReference& ref )
{

}

bool IndexBundleActivator::init_()
{
    laManager_ = createLAManager_();
    SF1R_ENSURE_INIT(laManager_);
    SF1R_ENSURE_INIT(initializeQueryManager_());
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open data directories.."<<std::endl;
    bool bOpenDataDir = openDataDirectories_();
    SF1R_ENSURE_INIT(bOpenDataDir);
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open id manager.."<<std::endl;
    idManager_ = createIDManager_();
    SF1R_ENSURE_INIT(idManager_);
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open index manager.."<<std::endl;
    indexManager_ = createIndexManager_();
    SF1R_ENSURE_INIT(indexManager_);
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open document manager.."<<std::endl;
    documentManager_ = createDocumentManager_();
    SF1R_ENSURE_INIT(documentManager_);
    documentManager_->setLangId(config_->languageIdentifierDbPath_);
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open ranking manager.."<<std::endl;
    rankingManager_ = createRankingManager_();
    SF1R_ENSURE_INIT(rankingManager_);
    std::cout<<"["<<config_->collectionName_<<"]"<<"[IndexBundleActivator] open search manager.."<<std::endl;
    searchManager_ = createSearchManager_();
    SF1R_ENSURE_INIT(searchManager_);
    pQA_ = Singleton<ilplib::qa::QuestionAnalysis>::get();

    searchService_ = new IndexSearchService;

    searchService_->laManager_ = laManager_;
    searchService_->idManager_ = idManager_;
    searchService_->documentManager_ = documentManager_;
    searchService_->indexManager_ = indexManager_;
    searchService_->rankingManager_ = rankingManager_;
    searchService_->searchManager_ = searchManager_;
    searchService_->pQA_ = pQA_;

    taskService_ = new IndexTaskService;

    taskService_->indexManager_ = indexManager_;
    taskService_->idManager_ = idManager_;
    taskService_->documentManager_ = documentManager_;
    taskService_->summarizer_.init(documentManager_->getLangId(), idManager_);

    return true;
}

std::string IndexBundleActivator::getCurrentCollectionDataPath_() const
{
    return config_->collPath_.getCollectionDataPath()+"/"+currentCollectionDataName_;
}

std::string IndexBundleActivator::getCollectionDataPath_() const
{
    return config_->collPath_.getCollectionDataPath();
}

std::string IndexBundleActivator::getQueryDataPath_() const
{
    return config_->collPath_.getQueryDataPath();
}

bool IndexBundleActivator::openDataDirectories_()
{
    std::vector<std::string>& directories = config_->collectionDataDirectories_;
    if( directories.size() == 0 )
    {
        std::cout<<"no data dir config"<<std::endl;
        return false;
    }
    directoryRotator_.setCapacity(directories.size());
    typedef std::vector<std::string>::const_iterator iterator;
    for (iterator it = directories.begin(); it != directories.end(); ++it)
    {
        bfs::path dataDir = bfs::path( getCollectionDataPath_() ) / *it;
        if (!directoryRotator_.appendDirectory(dataDir))
	{
	  std::string msg = dataDir.file_string() + " corrupted, delete it!";
	  sflog->error( SFL_SYS, msg.c_str() ); 
	  std::cout<<msg<<std::endl;
	  //clean the corrupt dir
	  boost::filesystem::remove_all( dataDir );
	  directoryRotator_.appendDirectory(dataDir);
	}
    }

    directoryRotator_.rotateToNewest();
    boost::shared_ptr<Directory> newest = directoryRotator_.currentDirectory();
    if (newest)
    {
	bfs::path p = newest->path();
	currentCollectionDataName_ = p.filename();
	sflog->info( SFL_IDX, 10206, getCurrentCollectionDataPath_().c_str() ); 

	//std::cout << "Current Index Directory: " << indexPath_() << std::endl;
	return true;
    }

    return false;
}

boost::shared_ptr<IDManager>
IndexBundleActivator::createIDManager_() const
{
    std::string dir = getCurrentCollectionDataPath_()+"/id/";
    boost::filesystem::create_directories(dir);

    boost::shared_ptr<IDManager> ret(
        new IDManager(dir)
    );

    return ret;
}

boost::shared_ptr<DocumentManager>
IndexBundleActivator::createDocumentManager_() const
{
    std::string dir = getCurrentCollectionDataPath_()+"/dm/";
    boost::filesystem::create_directories(dir);
    boost::shared_ptr<DocumentManager> ret(
        new DocumentManager(
            dir,
            config_->schema_,
            config_->encoding_,
            config_->documentCacheNum_
        )
    );

    return ret;
}

boost::shared_ptr<IndexManager>
IndexBundleActivator::createIndexManager_() const
{
    std::string dir = getCurrentCollectionDataPath_()+"/index/";
    boost::filesystem::create_directories(dir);
    boost::shared_ptr<IndexManager> ret;

    ret.reset(new IndexManager());
    if (ret)
    {
        IndexManagerConfig config(config_->indexConfig_);
        config.indexStrategy_.indexLocation_ = dir;

        std::map<std::string, unsigned int> collectionIdMapping;
        collectionIdMapping[config_->collectionName_] = 1;

        ret->setIndexManagerConfig(config, collectionIdMapping);
    }

    return ret;
}

boost::shared_ptr<RankingManager> 
IndexBundleActivator::createRankingManager_() const
{
    boost::shared_ptr<RankingManager> ret(new RankingManager);
    ret->init(config_->rankingManagerConfig_.rankingConfigUnit_);

    typedef std::map<std::string, float>::const_iterator weight_map_iterator;
    PropertyConfig propertyConfigOut;
    for (weight_map_iterator it = config_->rankingManagerConfig_.propertyWeightMapByProperty_.begin(),
                                        itEnd = config_->rankingManagerConfig_.propertyWeightMapByProperty_.end();
          it != itEnd; ++it)
    {
        if (config_->getPropertyConfig(it->first, propertyConfigOut))
        {
            ret->setPropertyWeight(propertyConfigOut.getPropertyId(), it->second);
        }
    }

    return ret;
}

boost::shared_ptr<SearchManager> 
IndexBundleActivator::createSearchManager_() const
{
    boost::shared_ptr<SearchManager> ret;

    if (indexManager_ && idManager_ && documentManager_ && rankingManager_)
    {
        ret.reset(
            new SearchManager(
                config_->schema_,
                idManager_,
                documentManager_,
                indexManager_,
                rankingManager_
            )
        );
    }
    else
    {
        sflog->info( SFL_IDX, 10218); 
    }

    return ret;
}

boost::shared_ptr<LAManager> 
IndexBundleActivator::createLAManager_() const
{
    boost::shared_ptr<LAManager> ret(new LAManager());
    std::string kma_path;
    LAPool::getInstance()->get_kma_path(kma_path);
    string temp = kma_path + "/stopword.txt";
    ret->loadStopDict( temp );
    return ret;
}

bool IndexBundleActivator::initializeQueryManager_() const
{
    // initialize Query Parser
    QueryParser::initOnlyOnce();

    std::string kma_path;
    LAPool::getInstance()->get_kma_path(kma_path);
    std::string restrictDictPath = kma_path + "/restrict.txt";
    QueryUtility::buildRestrictTermDictionary( restrictDictPath, idManager_);

    return true;
}


}

