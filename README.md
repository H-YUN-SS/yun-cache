# YUN-CACHE

从零实现的 C++ 多策略缓存框架，涵盖 LRU、LFU、ARC、LRU-K、LFU-Aging 五种经典淘汰算法。统一接口设计，线程安全，多场景压力测试对比命中率。

## 项目结构

```
yun-cache/
├── YICachePolicy.h             # 缓存统一抽象接口（纯虚基类）
├── YLruCache.h                 # LRU / LRU-K / 分片LRU
├── YLfuCache.h                 # LFU（含频率衰减）/ 分片LFU
├── YArcCache/
│   ├── YArcCache.h             # ARC 主类（LRU+LFU 双分区，自适应调容）
│   ├── YArcCacheNode.h         # ARC 节点
│   ├── YArcLruPart.h           # ARC 的 LRU 分区 + 幽灵缓存
│   └── YArcLfuPart.h           # ARC 的 LFU 分区 + 幽灵缓存
├── testAllCachePolicy.cpp      # 多场景对比测试
```

## 核心设计

### 统一抽象接口

所有算法继承同一个纯虚基类，测试中通过基类指针数组保证每个算法面对完全相同的访问序列：

```cpp
template<typename Key, typename Value>
class YICachePolicy {
public:
    virtual void put(Key key, Value value) = 0;
    virtual bool get(Key key, Value& value) = 0;
    virtual Value get(Key key) = 0;
};
```

### LRU — 哈希表 + 双向链表

```
dummyHead ⇄ Node1 ⇄ Node2 ⇄ ... ⇄ NodeN ⇄ dummyTail
                ↑
            nodeMap_[key] → NodePtr
```

- O(1) 读写，淘汰链表尾部（最久未访问）
- **LRU-K**：数据被访问 K 次后才进入主缓存，过滤一次性冷数据
- **分片 LRU**：缓存分成 N 片，每片独立锁，降低多线程锁冲突

### LFU — 频率链表 + 衰减机制

```
freq=1: Head ⇄ A ⇄ B ⇄ Tail
freq=2: Head ⇄ C ⇄ Tail
freq=5: Head ⇄ D ⇄ E ⇄ Tail
         ↑
     minFreq_（淘汰从这里找）
```

- 从最小频率链表淘汰最老节点
- **频率衰减**：平均访问频率超过阈值时全局频率减半，防止老热点永久占用缓存

### ARC — LRU + LFU 双分区 + 幽灵缓存

```
┌─────────────── ARC Cache (总容量 2C) ───────────────┐
│                                                      │
│   LRU 分区 (容量 p)        LFU 分区 (容量 C-p)       │
│   ┌──────────┐             ┌──────────┐             │
│   │  主缓存   │             │  主缓存   │             │
│   ├──────────┤             ├──────────┤             │
│   │  幽灵缓存  │             │  幽灵缓存  │             │
│   └──────────┘             └──────────┘             │
└──────────────────────────────────────────────────────┘

命中 LRU 幽灵 → LRU 太小 → 扩大 LRU，缩小 LFU
命中 LFU 幽灵 → LFU 太小 → 扩大 LFU，缩小 LRU
```

- 根据幽灵缓存命中动态调整两个分区的容量比例
- LRU 中被多次访问的数据自动晋升到 LFU 分区

### 线程安全

每个缓存类内部维护 `std::mutex`，通过 `std::lock_guard` 在 put/get 入口加锁。分片缓存进一步细化锁粒度，每片独立加锁支持并发读写。

## 编译 & 运行

```bash
g++ -std=c++17 -o testAllCachePolicy testAllCachePolicy.cpp
./testAllCachePolicy
```

## 测试结果

### 场景 1：热点数据访问

> 缓存 20，热点 key 20 个，冷数据 5000 个，50 万次操作，70% 读 / 30% 写，70% 访问热点

| 算法 | 命中率 |
|------|--------|
| LRU | 49.58% |
| **LFU-Aging** | **66.99%** |
| LFU | 66.85% |
| ARC | 65.96% |
| LRU-K | 55.17% |

LFU 系算法凭频率统计天然适合热点识别。

### 场景 2：循环顺序扫描

> 缓存 50，循环访问 key 0~499（远超缓存），20 万次操作，80% 读 / 20% 写，60% 顺序扫描

| 算法 | 命中率 |
|------|--------|
| **ARC** | **9.72%** |
| LFU-Aging | 9.00% |
| LFU | 8.88% |
| LRU-K | 4.81% |
| LRU | 4.45% |

循环扫描是 LRU 的经典弱点——顺序遍历把缓存中有用数据全部冲刷掉。ARC 通过幽灵缓存感知污染，自动调容缓解。

### 场景 3：工作负载剧烈变化

> 缓存 30，8 万次操作，分 5 个阶段（热点 → 大范围随机 → 顺序扫描 → 局部性随机 → 混合访问）

| 算法 | 命中率 |
|------|--------|
| **ARC** | **59.22%** |
| LRU | 54.99% |
| LRU-K | 54.51% |
| LFU-Aging | 39.87% |
| LFU | 39.22% |

模式不断切换时 ARC 自适应优势最明显。LFU 因历史频率累积，面对切换反应最慢。

### 总结

| 场景 | 最佳算法 | 原因 |
|------|---------|------|
| 热点访问 | LFU / LFU-Aging | 频率统计天然适合热点识别 |
| 循环扫描 | ARC | 幽灵缓存感知污染，自适应调容 |
| 模式切换 | ARC | 快速适应新访问模式，综合最强 |

## 依赖

- C++17（g++ 7+ / clang++ 5+）
- 无第三方库，纯标准库实现
