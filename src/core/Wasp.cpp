// FL 2026's Wasp / Wasp XT placeholder (REA_rodada64 §5): no voices, no sound.

#include "core/Wasp.h"

namespace lynx
{
namespace
{
class WaspInstrument final : public Instrument
{
public:
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}  // TriggerVoice returns no voice
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {} // Gen_Render writes nothing
    bool idle() const noexcept override { return true; }
};
} // namespace

std::unique_ptr<Instrument> makeWaspInstrument() { return std::make_unique<WaspInstrument>(); }
} // namespace lynx
