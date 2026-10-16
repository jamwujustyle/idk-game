#pragma once

#include <array>
#include <cstdint>

// This simulation has no engine dependencies. Distances use Unreal centimeters;
// all durations use seconds. Posture is accumulated damage, so a full bar breaks.
namespace ronin
{
enum class FighterId : std::uint8_t { Player, Enemy };
enum class Stance : std::uint8_t { Flow, Stone, Gale };
enum class AttackKind : std::uint8_t { Light, Heavy, Unblockable };
enum class CounterSource : std::uint8_t { None, Parry, Dodge };
enum class Action : std::uint8_t
{
    Idle, Windup, Active, Recovery, Dodging, Staggered, Broken, Dead
};
enum class HitResult : std::uint8_t
{
    None, Miss, Dodged, Parried, Blocked, Hit, GuardBreak
};

struct AttackSpec
{
    float windup = 0.f;
    float active = 0.f;
    float recovery = 0.f;
    float damage = 0.f;
    float postureDamage = 0.f;
    float range = 0.f;
    bool unblockable = false;
};

struct FighterState
{
    float health = 100.f;
    float maxHealth = 100.f;
    float posture = 0.f;
    float maxPosture = 100.f;
    Stance stance = Stance::Flow;
    Action action = Action::Idle;
    AttackKind currentAttack = AttackKind::Light;
    AttackSpec spec{};
    // Time elapsed in this phase, plus the total elapsed attack animation time.
    float actionTime = 0.f;
    float actionDuration = 0.f;
    float attackElapsed = 0.f;
    float parryTime = 0.f;
    float parryCooldown = 0.f;
    float invulnerabilityTime = 0.f;
    float counterTime = 0.f;
    float decisionTime = 0.f;
    float postureRecoveryDelay = 0.f;
    std::uint64_t attackId = 0;
    // A light chain has five deliberate strikes. Only one follow-up can be
    // queued at a time; attackId changes when it begins, not when it is queued.
    std::uint8_t comboStep = 0;
    bool comboQueued = false;
    // counterSource describes an available opening; attackCounterSource is
    // latched when that opening is spent and remains valid through recovery.
    CounterSource counterSource = CounterSource::None;
    CounterSource attackCounterSource = CounterSource::None;
    bool isCounterAttack = false;
    bool guarding = false;
    bool hitResolved = false;
    HitResult lastHit = HitResult::None;
};

class CombatCore
{
public:
    static constexpr float ParryWindow = 0.18f;
    static constexpr float ParryRetryDelay = 0.32f;
    static constexpr float ParryCounterWindow = 1.1f;
    static constexpr float DodgeCounterWindow = 0.65f;
    static constexpr std::uint8_t LightComboLength = 5;
    static constexpr float ComboLinkRecovery = 0.09f;
    static constexpr float CounterDamageMultiplier = 1.8f;
    static constexpr float CounterPostureMultiplier = 1.8f;
    static constexpr float CounterWindupMultiplier = 0.6f;
    static constexpr float ExecutionRange = 240.f;
    static constexpr float GuardBreakDuration = 2.2f;

    CombatCore();
    void Reset();
    void Tick(float deltaSeconds);
    const FighterState& Get(FighterId id) const;

    // True means a strike started OR one follow-up was queued. Presentation
    // should observe attackId to play each actual swing exactly once.
    bool TryAttack(FighterId id, AttackKind kind);
    // A physical press can cancel attack windup/recovery or a resolved strike.
    // Restoring a held button after an interruption
    // should pass allowParry=false once idle, so automatic blocking gives no parry.
    void SetGuard(FighterId id, bool held, bool allowParry = true);
    bool TryDodge(FighterId id);
    bool SetStance(FighterId id, Stance stance);

    // Call during Active with actual world-space distance and attack cone test.
    // A spatial miss can be retried; a valid contact is resolved only once.
    HitResult ResolveHit(FighterId attacker, float distance, bool facing = true);
    bool TryExecute(FighterId attacker, float distance, bool facing = true);

    // The engine supplies movement/spacing; this core supplies a repeatable rhythm.
    bool EnemyReady() const;
    AttackKind NextEnemyAttack() const;
    static AttackSpec GetAttackSpec(Stance stance, AttackKind kind);

private:
    std::array<FighterState, 2> fighters_{};
    std::uint64_t nextAttackId_ = 1;
    unsigned enemySequence_ = 0;

    FighterState& Mutable(FighterId id);
    static FighterId Opponent(FighterId id);
    static void SetAction(FighterState& fighter, Action action, float duration = 0.f);
    void StartAttack(FighterId id, AttackKind kind, std::uint8_t comboStep = 0);
    void TickFighter(FighterId id, float deltaSeconds);
    static void ClearChain(FighterState& fighter);
    static void ClearCounter(FighterState& fighter);
    static bool ApplyPosture(FighterState& fighter, float amount);
    static void Kill(FighterState& fighter);
};
} // namespace ronin
