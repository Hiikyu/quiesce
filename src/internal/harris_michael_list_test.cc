// Test suite for quiesce::intrusive::HarrisMichaelList.
//

#include "harris_michael_list.hxx"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <list>
#include <numeric>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using quiesce::intrusive::HarrisMichaelList;

// ---------------------------------------------------------------------
// Test node type
// ---------------------------------------------------------------------

struct alignas(alignof(uintptr_t)) TestNode {
  std::atomic<uintptr_t> next{0};
  int value = 0;

  // Test-only bookkeeping, not used by the list itself. Used by the
  // concurrent tests to detect double-delivery: at most one thread may
  // ever successfully exchange this from false to true for a given node.
  std::atomic<bool> claimed{false};

  TestNode() = default;
  explicit TestNode(int v) : value(v) {}
};

using TestList = HarrisMichaelList<TestNode, &TestNode::next>;

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

// Snapshot the values currently reachable via iteration, in order.
std::vector<int> SnapshotValues(TestList &list) {
  std::vector<int> out;
  for (auto &n : list)
    out.push_back(n.value);
  return out;
}

// Drain the list via repeated PopFront, recording the order popped.
std::vector<int> DrainValues(TestList &list) {
  std::vector<int> out;
  TestNode *n;
  while ((n = list.PopFront()) != nullptr)
    out.push_back(n->value);
  return out;
}

// Claims `node` exactly once; fails the current test if it was already
// claimed by someone else. No-op on nullptr.
void Claim(TestNode *node) {
  if (node == nullptr)
    return;
  bool already = node->claimed.exchange(true, std::memory_order_acq_rel);
  EXPECT_FALSE(already) << "node value=" << node->value
                        << " delivered more than once";
}

// ===========================================================================
// Single-threaded correctness tests
// ===========================================================================

TEST(Basic, EmptyListInvariants) {
  TestList list;
  EXPECT_EQ(list.begin(), list.end());
  EXPECT_EQ(list.PopFront(), nullptr);
  EXPECT_EQ(list.Clear(), nullptr);

  TestNode lone(1);
  EXPECT_FALSE(list.Erase(&lone));
  EXPECT_FALSE(list.InsertAfter(&lone, &lone));
  EXPECT_EQ((list.Find([](const TestNode &) { return true; })), nullptr);
  EXPECT_FALSE(list.Contains(&lone));
}

TEST(Basic, PushFrontSingleNode) {
  TestList list;
  TestNode a(1);
  list.PushFront(&a);

  ASSERT_NE(list.begin(), list.end());
  EXPECT_EQ(&*list.begin(), &a);
  EXPECT_EQ(list.begin()->value, 1);
}

TEST(Basic, PushFrontMultipleNodesLifoOrder) {
  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < 5; i++)
    nodes.emplace_back(i);
  for (auto &n : nodes)
    list.PushFront(&n); // push 0..4, front ends up 4..0

  EXPECT_EQ(SnapshotValues(list), (std::vector<int>{4, 3, 2, 1, 0}));
}

TEST(Basic, PopFrontOrderAndEmptying) {
  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < 3; i++)
    nodes.emplace_back(i);
  for (auto &n : nodes)
    list.PushFront(&n); // front: 2,1,0

  EXPECT_EQ(list.PopFront()->value, 2);
  EXPECT_EQ(list.PopFront()->value, 1);
  EXPECT_EQ(list.PopFront()->value, 0);
  EXPECT_EQ(list.PopFront(), nullptr);
  EXPECT_EQ(list.begin(), list.end());
}

TEST(Basic, InsertAfterHeadMiddleAndTail) {
  TestList list;
  TestNode a(1), b(2), c(3);
  list.PushFront(&c);
  list.PushFront(&b);
  list.PushFront(&a); // a, b, c

  TestNode mid(99);
  ASSERT_TRUE(list.InsertAfter(&a, &mid)); // a, mid, b, c
  EXPECT_EQ(SnapshotValues(list), (std::vector<int>{1, 99, 2, 3}));

  TestNode tailIns(100);
  ASSERT_TRUE(list.InsertAfter(&c, &tailIns)); // a, mid, b, c, tailIns
  EXPECT_EQ(SnapshotValues(list), (std::vector<int>{1, 99, 2, 3, 100}));
}

TEST(Basic, InsertAfterNotFoundReturnsFalse) {
  TestList list;
  TestNode a(1);
  list.PushFront(&a);

  TestNode notInList(2), toInsert(3);
  EXPECT_FALSE(list.InsertAfter(&notInList, &toInsert));
}

TEST(Basic, EraseHeadMiddleAndTail) {
  TestList list;
  TestNode a(1), b(2), c(3);
  list.PushFront(&c);
  list.PushFront(&b);
  list.PushFront(&a); // a, b, c

  ASSERT_TRUE(list.Erase(&b)); // a, c
  EXPECT_EQ(SnapshotValues(list), (std::vector<int>{1, 3}));

  ASSERT_TRUE(list.Erase(&a)); // c
  EXPECT_EQ(SnapshotValues(list), (std::vector<int>{3}));

  ASSERT_TRUE(list.Erase(&c)); // empty
  EXPECT_EQ(list.begin(), list.end());
}

TEST(Basic, EraseNotFoundOrTwiceReturnsFalse) {
  TestList list;
  TestNode a(1), b(2);
  list.PushFront(&a);

  EXPECT_FALSE(list.Erase(&b)); // never inserted
  EXPECT_TRUE(list.Erase(&a));  // first erase succeeds
  EXPECT_FALSE(list.Erase(&a)); // second erase on same node fails
}

TEST(Basic, FindReturnsFirstMatchNotLast) {
  TestList list;
  std::deque<TestNode> nodes;
  for (int v : {10, 20, 20, 30})
    nodes.emplace_back(v);
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
    list.PushFront(&*it);
  // list order: 10, 20, 20, 30

  TestNode *found = list.Find([](const TestNode &n) { return n.value == 20; });
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found, &nodes[1]); // the FIRST node with value 20

  EXPECT_EQ(list.Find([](const TestNode &n) { return n.value == 999; }),
            nullptr);
}

TEST(Basic, ContainsTracksMembership) {
  TestList list;
  TestNode a(1), b(2);
  list.PushFront(&a);

  EXPECT_TRUE(list.Contains(&a));
  EXPECT_FALSE(list.Contains(&b));

  ASSERT_TRUE(list.Erase(&a));
  EXPECT_FALSE(list.Contains(&a));
}

TEST(Basic, ClearDetachesTheWholeList) {
  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < 3; i++)
    nodes.emplace_back(i);
  for (auto &n : nodes)
    list.PushFront(&n); // front: 2,1,0

  TestNode *chain = list.Clear();
  EXPECT_EQ(list.begin(), list.end()); // original list now empty

  std::vector<int> got;
  uintptr_t raw = quiesce::RawPtr(chain);
  while (raw != 0) {
    TestNode *n = quiesce::Unmarked<TestNode>(raw);
    got.push_back(n->value);
    raw = quiesce::UnmarkedRaw(n->next.load(std::memory_order_acquire));
  }
  EXPECT_EQ(got, (std::vector<int>{2, 1, 0}));
}

TEST(Basic, ClearOnEmptyListReturnsNull) {
  TestList list;
  EXPECT_EQ(list.Clear(), nullptr);
}

TEST(Basic, EraseAtAdvancesAndRemovesDuringIteration) {
  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < 5; i++)
    nodes.emplace_back(i);
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
    list.PushFront(&*it);
  // order: 0,1,2,3,4

  std::vector<int> kept;
  for (auto it = list.begin(); it != list.end();) {
    if (it->value % 2 == 0) {
      it = list.EraseAt(it);
    } else {
      kept.push_back(it->value);
      ++it;
    }
  }
  EXPECT_EQ(kept, (std::vector<int>{1, 3}));
  EXPECT_EQ(SnapshotValues(list), (std::vector<int>{1, 3}));
}

TEST(Basic, EraseAtOnOnlyElementReturnsEnd) {
  TestList list;
  TestNode a(1);
  list.PushFront(&a);

  auto it = list.begin();
  auto next = list.EraseAt(it);
  EXPECT_EQ(next, list.end());
  EXPECT_EQ(list.begin(), list.end());
}

TEST(Basic, EraseAtSkipsOverAlreadyMarkedNeighbors) {
  // EraseAt's returned iterator must skip multiple consecutively-marked
  // nodes, not just hop one position.
  TestList list;
  TestNode a(1), b(2), c(3), d(4);
  list.PushFront(&d);
  list.PushFront(&c);
  list.PushFront(&b);
  list.PushFront(&a); // a, b, c, d

  ASSERT_TRUE(list.Erase(&c)); // mark+unlink c directly: a, b, d
  ASSERT_TRUE(list.Erase(&d)); // mark+unlink d directly: a, b

  auto it = list.begin(); // a
  it = list.EraseAt(it);  // erase a; next should be b (c, d already gone)
  ASSERT_NE(it, list.end());
  EXPECT_EQ(it->value, 2);
}

TEST(Basic, RangeForVisitsEachElementExactlyOnce) {
  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < 10; i++)
    nodes.emplace_back(i);
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
    list.PushFront(&*it);

  std::vector<int> seen;
  for (const auto &n : list)
    seen.push_back(n.value);

  std::vector<int> expected(10);
  std::iota(expected.begin(), expected.end(), 0);
  EXPECT_EQ(seen, expected);
}

TEST(Basic, StdFindIfWorksViaIteratorConformance) {
  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < 5; i++)
    nodes.emplace_back(i);
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
    list.PushFront(&*it);

  auto it = std::find_if(list.begin(), list.end(),
                         [](const TestNode &n) { return n.value == 3; });
  ASSERT_NE(it, list.end());
  EXPECT_EQ(it->value, 3);
}

TEST(Basic, PostIncrementReturnsPriorPosition) {
  TestList list;
  TestNode a(1), b(2);
  list.PushFront(&b);
  list.PushFront(&a); // a, b

  auto it = list.begin();
  auto prior = it++;
  EXPECT_EQ(prior->value, 1);
  EXPECT_EQ(it->value, 2);
}

TEST(Basic, IteratorSkipsNodeErasedMidTraversal) {
  // Simulate the "node erased between when the iterator landed on it and
  // when it advances" case without real concurrency.
  TestList list;
  TestNode a(1), b(2), c(3);
  list.PushFront(&c);
  list.PushFront(&b);
  list.PushFront(&a); // a, b, c

  auto it = list.begin();      // points at a
  ASSERT_TRUE(list.Erase(&a)); // erase a while `it` still holds it
  ++it;                        // must correctly skip past the removed a
  ASSERT_NE(it, list.end());
  EXPECT_EQ(it->value, 2);
}

#if defined(__cpp_lib_concepts)
static_assert(std::input_iterator<TestList::Iterator>,
              "Iterator must satisfy std::input_iterator");
static_assert(std::ranges::input_range<TestList>,
              "HarrisMichaelList must satisfy std::ranges::input_range");
#endif

TEST(Basic, ComplexSequenceMatchesReferenceModel) {
  // Cross-check against a simple, obviously-correct std::list doing the
  // same logical operations in the same order, single-threaded.
  TestList list;
  std::list<int> reference;

  std::deque<TestNode> nodes;
  for (int i = 0; i < 50; i++)
    nodes.emplace_back(i);

  for (int i = 0; i < 50; i++) {
    list.PushFront(&nodes[i]);
    reference.push_front(i);
  }
  ASSERT_EQ(SnapshotValues(list),
            (std::vector<int>(reference.begin(), reference.end())));

  // Erase every third element by value.
  for (int i = 0; i < 50; i += 3) {
    ASSERT_TRUE(list.Erase(&nodes[i]));
    reference.erase(std::find(reference.begin(), reference.end(), i));
  }
  ASSERT_EQ(SnapshotValues(list),
            (std::vector<int>(reference.begin(), reference.end())));

  // Insert a fresh node after every surviving even-valued node.
  std::deque<TestNode> inserted;
  int insertedValueBase = 1000;
  for (auto refIt = reference.begin(); refIt != reference.end();) {
    int v = *refIt;
    ++refIt;
    if (v % 2 == 0 && v % 3 != 0) {
      inserted.emplace_back(insertedValueBase);
      ASSERT_TRUE(list.InsertAfter(&nodes[v], &inserted.back()));
      reference.insert(refIt, insertedValueBase);
      insertedValueBase++;
    }
  }
  ASSERT_EQ(SnapshotValues(list),
            (std::vector<int>(reference.begin(), reference.end())));

  // Drain everything and compare final order.
  EXPECT_EQ(DrainValues(list),
            (std::vector<int>(reference.begin(), reference.end())));
}

// ===========================================================================
// Concurrent access pattern tests
// ===========================================================================

TEST(Concurrent, PushFrontFromManyThreadsLosesNothing) {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 4000;
  TestList list;

  std::deque<TestNode> nodes;
  for (int i = 0; i < kThreads * kPerThread; i++)
    nodes.emplace_back(i);

  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; i++) {
        list.PushFront(&nodes[t * kPerThread + i]);
      }
    });
  }
  for (auto &th : threads)
    th.join();

  std::unordered_set<int> seen;
  TestNode *n;
  while ((n = list.PopFront()) != nullptr) {
    auto [it, inserted] = seen.insert(n->value);
    EXPECT_TRUE(inserted) << "value " << n->value
                          << " delivered more than once";
  }
  EXPECT_EQ(seen.size(), static_cast<size_t>(kThreads * kPerThread));
}

TEST(Concurrent, ProducerConsumerExactlyOnceDelivery) {
  constexpr int kProducers = 4;
  constexpr int kConsumers = 4;
  constexpr int kPerProducer = 5000;
  constexpr int kTotal = kProducers * kPerProducer;

  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < kTotal; i++)
    nodes.emplace_back(i);

  std::atomic<bool> producingDone{false};
  std::atomic<int> totalPopped{0};

  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; p++) {
    producers.emplace_back([&, p] {
      for (int i = 0; i < kPerProducer; i++) {
        list.PushFront(&nodes[p * kPerProducer + i]);
      }
    });
  }

  std::vector<std::vector<int>> perConsumerResults(kConsumers);
  std::vector<std::thread> consumers;
  for (int c = 0; c < kConsumers; c++) {
    consumers.emplace_back([&, c] {
      for (;;) {
        TestNode *n = list.PopFront();
        if (n != nullptr) {
          perConsumerResults[c].push_back(n->value);
          totalPopped.fetch_add(1, std::memory_order_relaxed);
        } else if (producingDone.load(std::memory_order_acquire) &&
                   totalPopped.load(std::memory_order_acquire) >= kTotal) {
          break;
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto &th : producers)
    th.join();
  producingDone.store(true, std::memory_order_release);
  for (auto &th : consumers)
    th.join();

  std::unordered_set<int> all;
  size_t totalCount = 0;
  for (auto &v : perConsumerResults) {
    for (int x : v) {
      auto [it, inserted] = all.insert(x);
      EXPECT_TRUE(inserted)
          << "value " << x << " delivered to more than one consumer";
      totalCount++;
    }
  }
  EXPECT_EQ(totalCount, static_cast<size_t>(kTotal));
  EXPECT_EQ(all.size(), static_cast<size_t>(kTotal));
}

// This is the test most directly tied to the design's central invariant:
// marking is the single linearization point for removal, so PopFront and
// Erase racing on the same node must never both report success for it.
TEST(Concurrent, PopFrontAndEraseNeverBothClaimTheSameNode) {
  constexpr int kRounds = 3000;
  constexpr int kEraserThreads = 3;
  constexpr int kPopperThreads = 3;

  for (int round = 0; round < kRounds; round++) {
    TestList list;
    TestNode node(round);
    list.PushFront(&node);

    std::atomic<int> claims{0};
    std::vector<std::thread> threads;
    threads.reserve(kEraserThreads + kPopperThreads);

    for (int i = 0; i < kEraserThreads; i++) {
      threads.emplace_back([&] {
        if (list.Erase(&node))
          claims.fetch_add(1, std::memory_order_relaxed);
      });
    }
    for (int i = 0; i < kPopperThreads; i++) {
      threads.emplace_back([&] {
        if (list.PopFront() != nullptr)
          claims.fetch_add(1, std::memory_order_relaxed);
      });
    }
    for (auto &th : threads)
      th.join();

    ASSERT_EQ(claims.load(), 1)
        << "round " << round << ": node claimed " << claims.load()
        << " times (expected exactly 1)";
  }
}

// Multiple threads racing Erase() on the exact same target: exactly one
// may succeed.
TEST(Concurrent, EraseRacingWithItselfHasExactlyOneWinner) {
  constexpr int kRounds = 3000;
  constexpr int kThreads = 6;

  for (int round = 0; round < kRounds; round++) {
    TestList list;
    TestNode node(round);
    list.PushFront(&node);

    std::atomic<int> wins{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; i++) {
      threads.emplace_back([&] {
        if (list.Erase(&node))
          wins.fetch_add(1, std::memory_order_relaxed);
      });
    }
    for (auto &th : threads)
      th.join();

    ASSERT_EQ(wins.load(), 1) << "round " << round;
  }
}

// This is the second historical bug this design hunts: InsertAfter's CAS
// must correctly detect when its `after` anchor is concurrently removed,
// so a reported success never results in a silently-lost node.
TEST(Concurrent, InsertAfterNeverLosesANodeWhenAnchorPopsConcurrently) {
  constexpr int kRounds = 2000;
  constexpr int kInserterThreads = 4;

  for (int round = 0; round < kRounds; round++) {
    TestList list;
    TestNode anchor(-1);
    list.PushFront(&anchor);

    std::deque<TestNode> toInsert;
    for (int i = 0; i < kInserterThreads; i++)
      toInsert.emplace_back(round * 100 + i);

    std::atomic<int> insertedCount{0};
    std::vector<std::thread> threads;
    threads.reserve(kInserterThreads + 1);

    for (int i = 0; i < kInserterThreads; i++) {
      threads.emplace_back([&, i] {
        if (list.InsertAfter(&anchor, &toInsert[i]))
          insertedCount.fetch_add(1, std::memory_order_relaxed);
      });
    }
    threads.emplace_back([&] { list.PopFront(); }); // races to remove `anchor`

    for (auto &th : threads)
      th.join();

    std::unordered_set<TestNode *> remaining;
    TestNode *n;
    while ((n = list.PopFront()) != nullptr)
      remaining.insert(n);

    int found = 0;
    for (auto &node : toInsert) {
      if (remaining.count(&node))
        found++;
    }
    ASSERT_EQ(found, insertedCount.load())
        << "round " << round << ": " << insertedCount.load()
        << " InsertAfter calls reported success but only " << found
        << " of those nodes are actually reachable";
  }
}

// EraseAt provides no direct success signal, so it's verified differently
// from Erase/PopFront: rather than counting individual claims, this
// checks the final structural invariant — every node matching the erase
// predicate is gone, and every node that should have survived is present
// exactly once.
TEST(Concurrent, EraseAtRemovesExactlyTheMatchingNodes) {
  constexpr int kNodeCount = 12000;
  constexpr int kThreads = 6;

  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < kNodeCount; i++)
    nodes.emplace_back(i);
  for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
    list.PushFront(&*it);

  auto shouldRemove = [](const TestNode &n) { return (n.value % 3) == 0; };

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([&] {
      for (auto it = list.begin(); it != list.end();) {
        if (shouldRemove(*it))
          it = list.EraseAt(it);
        else
          ++it;
      }
    });
  }
  for (auto &th : threads)
    th.join();

  for (const auto &n : list) {
    EXPECT_FALSE(shouldRemove(n))
        << "value " << n.value << " should have been erased";
  }

  std::unordered_map<int, int> remainingCounts;
  for (const auto &n : list)
    remainingCounts[n.value]++;

  for (auto &node : nodes) {
    if (!shouldRemove(node)) {
      EXPECT_EQ(remainingCounts[node.value], 1)
          << "surviving value " << node.value << " missing or duplicated";
    }
  }
}

// Full mixed-operation stress: every method, racing against every other
// method, on a shared pool. Verifies the universal exactly-once-delivery
// invariant across PushFront/PopFront/Erase/iteration together, and is
// the test most likely to surface an unanticipated interaction. Mainly a
// crash/TSan-catching smoke test; pair with -fsanitize=thread.
TEST(Concurrent, MixedOperationsStressNoDoubleDelivery) {
  constexpr int kNodeCount = 20000;
  constexpr int kThreads = 8;
  constexpr int kOpsPerThread = kNodeCount / kThreads * 3;

  TestList list;
  std::deque<TestNode> nodes;
  for (int i = 0; i < kNodeCount; i++)
    nodes.emplace_back(i);
  for (auto &n : nodes)
    list.PushFront(&n);

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([&, t] {
      std::mt19937 rng(static_cast<unsigned>(t) * 7919u + 17u);
      std::uniform_int_distribution<int> op(0, 2);
      std::uniform_int_distribution<size_t> pick(0, nodes.size() - 1);

      for (int i = 0; i < kOpsPerThread; i++) {
        switch (op(rng)) {
        case 0:
          Claim(list.PopFront());
          break;
        case 1: {
          TestNode &target = nodes[pick(rng)];
          if (list.Erase(&target))
            Claim(&target);
          break;
        }
        case 2: {
          // Best-effort traversal; concurrent mutation may shrink what's
          // visible — that's fine, it just must never crash or observe
          // a torn node.
          for (auto it = list.begin(); it != list.end(); ++it) {
            volatile int v = it->value;
            (void)v;
          }
          break;
        }
        }
      }
    });
  }
  for (auto &th : threads)
    th.join();

  // Drain whatever's left; every node must be accounted for exactly once
  // across the stress phase plus this final drain.
  TestNode *n;
  while ((n = list.PopFront()) != nullptr)
    Claim(n);

  for (auto &node : nodes) {
    EXPECT_TRUE(node.claimed.load())
        << "node value=" << node.value << " never accounted for";
  }
}

} // namespace
