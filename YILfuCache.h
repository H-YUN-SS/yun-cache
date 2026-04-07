#pragma once
#include <unordered_map>
#include <list>
#include <mutex>
#include <memory>
#include "YICachePolicy.h"

template <typename Key,typename Value>
struct LfuNode
{
    Key key_;
    Value value_;
    int freq_;// 访问次数（LFU 核心：频率越低越先被淘汰）

    LfuNode(Key k,Value v,int f):key_(k),value_(v),freq_(f){};
};

template <typename Key,typename Value>
class YILfuCache : public YICachePolicy<Key,Value>
{
public:
    using Node = LfuNode<Key,Value>;
    using NodePtr=std::shared_ptr<Node>;

    explicit YILfuCache(int capacity):capacity_(capacity){}

    void put(Key key,Value value)override
    {
        std::lock_guard<std::mutex>lock(mutex_);
        if (capacity_<= 0)
        {
            return;
        }
        if(nodeMap_.count(key))
        {
            updateNode(key,value);
            return;
        }
        if(nodeMap_.size() >= capacity_)
        {
            evictLeastFrequent();
        }
    
        NodePtr newNode=std::make_shared<Node>(key,value,1);
        nodeMap_[key]=newNode;
        freqMap_[1].push_front(key);
        keyIterMap_[key]=freqMap_[1].begin();
        minFreq_=1;
    }

    bool get(Key key,Value& value)override
    {
        std::lock_guard<std::mutex>lock(mutex_);
        if(!nodeMap_.count(key))
        {
            return false;
        }
    
        NodePtr node=nodeMap_[key];
        value=node->value_;
        increaseFrequency(key);
        return true;
    }

    Value get(Key key)override
    {
        Value val{};
        get(key,val);
        return val;
    }



    private:
    void updateNode(Key key,Value value)
    {
        NodePtr node = nodeMap_[key];
        node->value_=value;
        increaseFrequency(key);

    }

    void increaseFrequency(Key key)
    {
        NodePtr node =nodeMap_[key];
        int oldFreq = node->freq_;

        freqMap_[oldFreq].erase(keyIterMap_[key]);
        if(freqMap_[oldFreq].empty()&&oldFreq==minFreq_)
        {
            minFreq_++;
        }
        node->freq_++;
        int newFreq=oldFreq+1;
        freqMap_[newFreq].push_front(key);
        keyIterMap_[key]=freqMap_[newFreq].begin();
    }
    // 淘汰：删除频率最低、最久未用的 key
    void evictLeastFrequent()
    {
        auto& leastFreqList =freqMap_[minFreq_];
        Key deleteKey =leastFreqList.back();
        leastFreqList.pop_back();

        nodeMap_.erase(deleteKey);
        keyIterMap_.erase(deleteKey);
    }
    private:
    int capacity_;      // 缓存容量
    int minFreq_=0;     // 当前最小频率（LFU 核心）

    std::unordered_map<Key,NodePtr>nodeMap_;
    std::unordered_map<int,std::list<Key>>freqMap_;
    std::unordered_map<Key,typename std::list<Key>::iterator>keyIterMap_;

    std::mutex mutex_;

};