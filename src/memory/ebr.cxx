#include "../internal/harris_michael_list.hxx"
#include <atomic>
#include <limits>

namespace quiesce::memory {

using namespace quiesce;

struct EBRParticipant {
  std::atomic<uint64_t> localEpoch;
  std::atomic<uintptr_t> next;
};

class EBRDomain {

  static constexpr uint64_t INACTIVE_EPOCH =
      std::numeric_limits<uint64_t>::max();

public:
  EBRDomain() : epoch(INACTIVE_EPOCH) {}

private:
  std::atomic<uint64_t> epoch;
  intrusive::HarrisMichaelList<EBRParticipant, &EBRParticipant::next>
      participants;
};

} // namespace quiesce::memory