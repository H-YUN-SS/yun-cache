#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>
#include <array>

#include "YICachePolicy.h"
#include "YLfuCache.h"
#include "YLruCache.h"
#include "YArcCache/YArcCache.h"


class Timer
{
    public :
    //创建时自动记录开始时间
    Timer():start_(std::chrono::high_resolution_clock::now()){}

    //计算距离创建用了多长时间
    double elapsed()
    {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    }

    private:
    std::chrono::time_point<std::chrono::high_resolution_clock>start_;
};


//统一打印测试结果（命中率）
//参数：测试名、缓存大小、get次数、命中次数
void printResults(const std::string& testName,
                    int capacity,
                    const std::vector<int>&get_operations,
                    const std::vector<int>&hits)
{
    std::cout<<"==="<<testName<<" 结果汇总 ==="<<std::endl;
    std::cout<<"缓存大小 "<<capacity<<std::endl;

    //根据结果数量 自动匹配算法名
    std::vector<std::string>names;
    if(hits.size()==3)
    {
        names={"LRU","LFU","ARC"};
    }
    else if(hits.size()==4)
    {
        names={"LRU","LFU","ARC","LRU-K"};
    }
    else if(hits.size()==5)
    {
        names={"LRU","LFU","ARC","LRU-K","LFU-Aging"};
    }

    //循环打印每个算法命中率
    for(size_t i = 0;i<hits.size();i++)
    {
        //命中率 = 命中次数 / 总get次数
        double hitRate = 100.0*hits[i]/get_operations[i];
        std::cout<<names[i]<<" 命中率 :"<<std::fixed<<std::setprecision(2)<<hitRate<<"%";
        //格式：命中次数/总次数
        std::cout<<"("<<hits[i]<<"/"<<get_operations[i]<<")"<<std::endl;
        std::cout<<"---------------------"<<std::endl;
    }
}


//测试 大量访问热点key+少量冷数据
//作用 对比哪一项能留住热点 不被冷数据污染

void testHotDataAccess()
{
    std::cout<<"=== 测试场景1 ：热点数据访问测试 ==="<<std::endl;
    //缓存容量
    const int CAPACITY = 20;
    //总操作次数
    const int OPERATIONS = 500000;
    //热点数据：20个
    const int HOT_KEYS=20;
    //冷数据5000个
    const int COLD_KEYS=5000;

    //创建五个缓存对象
    YCache::YLruCache<int,std::string>lru(CAPACITY);
    YCache::YLfuCache<int,std::string>lfu(CAPACITY);
    YCache::YArcCache<int,std::string>arc(CAPACITY);
    YCache::YLruKCache<int,std::string>lruK(CAPACITY,HOT_KEYS + COLD_KEYS,2);
    YCache::YLfuCache<int,std::string> lfuAging(CAPACITY,20000);

    //随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());

    //用基类接口统一管理5个缓存
    std::array<YCache::YICachePolicy<int,std::string>*,5> caches={&lru,&lfu,&arc,&lruK,&lfuAging};

    //记录数据
    std::vector<int>hits(5,0);//命中次数
    std::vector<int>get_operations(5,0);//get次数

    //真正测试逻辑
    //对每一个缓存，执行完全一样的访问序列
    for(int i = 0;i<caches.size();i++)
    {
        //预热：先放入热点数据
        for(int key = 0;key<HOT_KEYS;key++)
        {
            std::string value = "value" + std::to_string(key);
            caches[i]->put(key,value);
        }

        //执行50w次
        for(int op = 0; op <OPERATIONS;++op)
        {
            //30概率 put ，70概率get
            bool isPut(gen()%100<30);
            int key;

            //70概率访问热点， 30访问冷数据
            if(gen()%100 <70)
            {
                key = gen()%HOT_KEYS;
            }
            else 
            {
                key = HOT_KEYS + (gen()%COLD_KEYS);
            }

            if(isPut)
            {
                //写操作
                std::string value = "value"+std::to_string(key);
                caches[i]->put(key,value);
            }
            else{
                //读操作 统计命中
                std::string result;
                get_operations[i]++;
                if(caches[i]->get(key,result))
                {
                    hits[i]++;
                }
            }
        }
    }
    printResults("热点数据访问测试", CAPACITY, get_operations, hits);

}

//测试 循环顺序访问
//作用 LRU最差场景 看ARC/LFU表现
void testLoopPattern()
{
    std::cout<<"\n=== 测试场景2 ：循环扫描测试 ==="<<std::endl;
    
    const int CAPACITY = 50;
    const int LOOP_SIZE = 500;      //循环0-499
    const int OPERATIONS = 200000;

    //创建5个缓存
    YCache::YLruCache<int,std::string> lru(CAPACITY);
    YCache::YLfuCache<int,std::string> lfu(CAPACITY);
    YCache::YArcCache<int,std::string> arc(CAPACITY);
    YCache::YLruKCache<int,std::string> lruk(CAPACITY,LOOP_SIZE*2,2);
    YCache::YLfuCache<int,std::string> lfuAging(CAPACITY,3000);

    
    //用基类数组管理所有缓存
    std::array<YCache::YICachePolicy<int,std::string>*,5>caches = {&lru,&lfu,&arc,&lruk,&lfuAging};

    //命中次数数组
    std::vector<int>hits(5,0);

    //查询次数数组
    std::vector<int>get_operations(5,0);
    
    //算法名称
    std::vector<std::string> names = {"LRU","LFU","ARC","LRU-K","LFU-Aging"};

    //随机数生成
    std::random_device rd;
    std::mt19937 gen(rd());
    for(int i=0;i<caches.size();i++)
    {
        //预热 提前放入部分数据
        for(int key = 0 ; key<LOOP_SIZE/5;key++)
        {
            std::string value = "loop" + std::to_string(key);
            caches[i] -> put(key,value);
        }
        //当前循环访问位置 从0开始
        int current_pos = 0;


        for(int op = 0; op<OPERATIONS;++op)
        {
            //20概率写 80概率读
            bool isput = (gen()%100<20);
            int key;

            //60概率： 顺序访问（循环扫描）
            if(op %100 <60)
            {
                key = current_pos;
                current_pos = (current_pos +1)%LOOP_SIZE;
            }
            //30概率 随机访问
            else if(op%100 <90)
            {
                key = gen()%LOOP_SIZE;
            }
            //10概率 访问范围外数据
            else 
            {
                key = LOOP_SIZE + (gen()%LOOP_SIZE);
            }
            if(isput)
            {
                std::string value = "loop" + std::to_string(key) + "_v" +std::to_string(op%100);
                caches[i]->put(key,value);
            }
            else 
            {
                std::string result;
                get_operations[i]++;
                if(caches[i]->get(key,result))
                {
                    hits[i]++;
                }
            }


        }
    }
    printResults("循环扫描测试",CAPACITY,get_operations,hits);
}

//测试 工作负荷剧烈变化测试
//访问模式不断变化：热点→随机→顺序→局部→混合
//测试目标：考验缓存的自适应能力（ARC 最强）

void testWorkloadShift()
{
    std::cout<<"\n=== 测试场景3 ：工作复合剧烈变化测试 ==="<<std::endl;

    const int CAPACITY = 30;                    // 缓存容量
    const int OPERATIONS = 80000;               // 总操作次数
    const int PHASE_LENGTH = OPERATIONS / 5;    // 分为 5 个阶段
    
    //创建5个缓存对象
    YCache::YLruCache<int, std::string> lru(CAPACITY);
    YCache::YLfuCache<int, std::string> lfu(CAPACITY);
    YCache::YArcCache<int, std::string> arc(CAPACITY);
    YCache::YLruKCache<int, std::string> lruk(CAPACITY, 500, 2);
    YCache::YLfuCache<int, std::string> lfuAging(CAPACITY, 10000);

    std::random_device rd;
    std::mt19937 gen(rd());

    std::array<YCache::YICachePolicy<int, std::string>*, 5> caches = {&lru, &lfu, &arc, &lruk, &lfuAging};
    //命中次数
    std::vector<int>hits(5,0);

    //查询次数
    std::vector<int>get_operations(5,0);

    //算法名称
    std::vector<std::string>names = {"LRU", "LFU", "ARC", "LRU-K", "LFU-Aging"};


    //每个算法执行相同测试
    for(int i=0;i<caches.size();i++)
    {
        //预热
        for(int key=0;key<30;++key)
        {
            std::string value = "init" + std::to_string(key);
            caches[i]->put(key,value);
        }

        //8w次操作，分5个阶段
        for(int op=0;op<OPERATIONS;++op)
        {
            //当前处于第几阶段
            int phase = op/PHASE_LENGTH;

            //每个阶段读写概率不同
            int putProbability;
            switch(phase)
            {
                case 0:putProbability = 15;break;   // 热点阶段 15%写
                case 1:putProbability = 30;break;   // 阶段2: 大范围随机，写比例为30%
                case 2:putProbability = 10;break;   // 阶段3: 顺序扫描，10%写入保持不变
                case 3:putProbability = 25;break;   // 阶段4: 局部性随机，微调为25%
                case 4:putProbability = 20;break;   // 阶段5: 混合访问，调整为20%
                default:putProbability = 20;break;
            }

            bool isPut(gen()%100<putProbability);

            // 根据不同阶段选择不同的访问模式生成key - 优化后的访问范围


            int key;
            if (op < PHASE_LENGTH) 
            {  // 阶段1: 热点访问 - 热点数量5，使热点更集中
                key = gen() % 5;
            } else if (op < PHASE_LENGTH * 2) 
            {  // 阶段2: 大范围随机 - 范围400，更适合30大小的缓存
                key = gen() % 400;
            } else if (op < PHASE_LENGTH * 3) 
            {  // 阶段3: 顺序扫描 - 保持100个键
                key = op  % 100;
            } else if (op < PHASE_LENGTH * 4) 
            {  // 阶段4: 局部性随机 - 优化局部性区域大小
                // 产生5个局部区域，每个区域大小为15个键，与缓存大小20接近但略小
                int locality = (op / 800) % 5;  // 调整为5个局部区域
                key = locality * 15 + (gen() % 15);  // 每区域15个键

            } else 
            {  // 阶段5: 混合访问 - 增加热点访问比例
                int r = gen() % 100;
                if (r < 40) 
                {  // 40%概率访问热点（从30%增加）
                    key = gen() % 5;  // 5个热点键
                } else if (r < 70) 
                {  // 30%概率访问中等范围
                    key = 5 + (gen() % 45);  // 缩小中等范围为50个键
                } else 
                {  // 30%概率访问大范围（从40%减少）
                    key = 50 + (gen() % 350);  // 大范围也相应缩小
                }
            }
            
            if (isPut) 
            {
                // 执行写操作
                std::string value = "value" + std::to_string(key) + "_p" + std::to_string(phase);
                caches[i]->put(key, value);
            } else 
            {
                // 执行读操作并记录命中情况
                std::string result;
                get_operations[i]++;
                if (caches[i]->get(key, result)) 
                {
                    hits[i]++;
                }
            }
            
        }
    }

     printResults("工作负载剧烈变化测试", CAPACITY, get_operations, hits);
}

int main() 
{
    testHotDataAccess();
    testLoopPattern();
    testWorkloadShift();
    return 0;
}