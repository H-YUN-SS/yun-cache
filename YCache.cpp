#include "YCache.h"

YCache::YCache(int max_size)
{
    max_size_= max_size;
}

void YCache::set(const std::string& key,const std::string& value,int expire_seconds)
{
    if(cache_.size()>=max_size_)
    {
        lru_eliminate();
    }
    CacheNode node;
    node.value=value;
    if(expire_seconds>0)
    {
        //time(NULL)	C 标准库函数，获取当前系统时间的 Unix 时间戳（单位：秒）
        node.expire=time(NULL)+expire_seconds; 
    }
    else 
    {
        node.expire = 0;
    }
    cache_[key]=node;
}

bool YCache::get(const std::string& key,std::string& value)
{
    auto it =cache_.find(key);
    if(it==cache_.end())
    {
        return false;
    }
    if(is_expire(key))
    {
        cache_.erase(it);
        return false;
    }
    it->second.LRU_time=time(NULL);
    value=it->second.value;
    return true;
}

void YCache::del(const std::string& key)
{
    cache_.erase(key);
}

bool YCache::is_expire(const std::string& key)
{
    auto it=cache_.find(key);
    if(it==cache_.end())
    {
        return true;
    }
    if(it->second.expire==0)
    {
        return false;
    }
    return time(NULL)>it->second.expire;
}
void YCache::lru_eliminate()
{
    std::string delete_key;
    time_t min_time=INT_MAX;
    for(auto& pair:cache_)
    {
        if(pair.second.LRU_time<min_time)
        {
            min_time=pair.second.LRU_time;
            delete_key=pair.first;
        }
    }
    if(!delete_key.empty())
    {
        cache_.erase(delete_key);
    }
}