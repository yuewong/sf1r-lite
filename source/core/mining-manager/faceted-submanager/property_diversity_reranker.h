/**
 * @file property_diversity_reranker.h
 * @author Yingfeng Zhang
 * @date Jun 28, 2011
 * @brief Providing simple diversity reranking according to property
 * @description It depends on GroupManager from MiningManager, and needs the 
 * corresponding property to be configured in mining schema
 */
#ifndef PROPERTY_DIVERSITY_RERANKER_H
#define PROPERTY_DIVERSITY_RERANKER_H

#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include "group_manager.h"

#include <common/inttypes.h>
#include <util/ustring/UString.h>

namespace sf1r
{
class GroupLabelLogger;
}

NS_FACETED_BEGIN

using sf1r::faceted::GroupManager;

class PropertyDiversityReranker
{
public:
    PropertyDiversityReranker(
        const std::string& property,
        GroupManager* groupManager,
        const std::string& boostingProperty
    );

    ~PropertyDiversityReranker();

    void rerank(
        std::vector<unsigned int>& docIdList,
        std::vector<float>& rankScoreList,
        const std::string& query
    );

    void simplererank(
        std::vector<unsigned int>& docIdList,
        std::vector<float>& rankScoreList
    );

    void setGroupLabelLogger(GroupLabelLogger* logger){
        groupLabelLogger_ = logger;
    }
private:
    std::string property_;
    GroupManager* groupManager_;
    GroupLabelLogger* groupLabelLogger_;
    std::string boostingProperty_;
};


NS_FACETED_END


#endif /* PROPERTY_DIVERSITY_RERANKER_H */

