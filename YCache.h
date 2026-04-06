#pragma once

#include <iostream>
#include <unordered_map>
#include <string>
#include <ctime>

struct  CacheNode
{
    std::string value;  //缓存数据
    time_t expire;      //过期时间戳
    time_t LRU_time;    //最后访问时间
};

class YCache 
{
    public:
    YCache(int max_size = 1024);
    void set(const std::string&key,const std::string& value, int expire_seconds=-1);
    bool get(const std::string&key,std::string& value);
    void del(const std::string& key);
    int ttl(const std::string&key);

    private:
    bool is_expire(const std::string&key);
    void lru_eliminate();
    int max_size_;
    std::unordered_map<std::string,CacheNode>cache_;


};