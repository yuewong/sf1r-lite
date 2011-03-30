#include "SearchHelper.h"

namespace sf1r
{

void assembleConjunction(std::vector<izenelib::util::UString> keywords, std::string& result)
{
    result.clear();
    int size = keywords.size();
    for(int i = 0; i < size; ++i)
    {
        std::string str;
        keywords[i].convertString(str, izenelib::util::UString::UTF_8);
        result += str;
        result += " ";
    }
}

void assembleDisjunction(std::vector<izenelib::util::UString> keywords, std::string& result)
{
    result.clear();
    int size = keywords.size();
    for(int i = 0; i < size; ++i)
    {
        std::string str;
        keywords[i].convertString(str, izenelib::util::UString::UTF_8);
        result += str;
        result += "|";
    }
}

bool buildQueryTree(SearchKeywordOperation&action, IndexBundleConfiguration& bundleConfig, std::string& btqError)
{
    action.clear();
    KeywordSearchActionItem actionItem = action.actionItem_;

    // Build raw Query Tree
    UString::EncodingType encodingType =
        UString::convertEncodingTypeFromStringToEnum(
            action.actionItem_.env_.encodingType_.c_str() );
    UString queryUStr(action.actionItem_.env_.queryString_, encodingType);
    if ( !action.queryParser_.parseQuery( queryUStr, action.rawQueryTree_, action.unigramFlag_ ) )
        return false;
    action.rawQueryTree_->print();

    // Build property query map
    bool applyLA = action.actionItem_.languageAnalyzerInfo_.applyLA_;

    std::vector<std::string>::const_iterator propertyIter = action.actionItem_.searchPropertyList_.begin();
    for (; propertyIter != action.actionItem_.searchPropertyList_.end(); propertyIter++)
    {
        // If there's already processed query, skip processing of this property..
        if ( action.queryTreeMap_.find( *propertyIter ) != action.queryTreeMap_.end()
                && action.propertyTermInfo_.find( *propertyIter ) != action.propertyTermInfo_.end() )
            continue;

        QueryTreePtr tmpQueryTree;
        if ( applyLA )
        {
            AnalysisInfo analysisInfo;
            std::string analysis, language;
            bundleConfig.getAnalysisInfo( *propertyIter, analysisInfo, analysis, language );

            if ( !action.queryParser_.getAnalyzedQueryTree(
                        action.actionItem_.languageAnalyzerInfo_.synonymExtension_,
                        analysisInfo, queryUStr, tmpQueryTree, action.unigramFlag_ ))
                return false;
        } // end - if
        else // store raw query's info into it.
            tmpQueryTree = action.rawQueryTree_;
        std::cout << "Property " << *propertyIter << std::endl;
        tmpQueryTree->print();
        action.queryTreeMap_.insert( std::make_pair(*propertyIter,tmpQueryTree) );
        PropertyTermInfo ptInfo;
        tmpQueryTree->getPropertyTermInfo(ptInfo);
        action.propertyTermInfo_.insert( std::make_pair(*propertyIter,ptInfo) );

    } // end - for
    return true;
} // end - buildQueryTree()


}
