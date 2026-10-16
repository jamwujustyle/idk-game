#include "CombatCore.h"

#include <algorithm>
#include <cmath>

namespace ronin
{
namespace
{
constexpr float Epsilon = 0.00001f;

void CountDown(float& timer, float seconds)
{
    timer = std::max(0.f, timer - seconds);
}

bool IsAttacking(Action action)
{
    return action == Action::Windup || action == Action::Active || action == Action::Recovery;
}

bool CanDefend(const FighterState& fighter)
{
    return fighter.action == Action::Idle || fighter.action == Action::Windup ||
        fighter.action == Action::Recovery ||
        (fighter.action == Action::Active && fighter.hitResolved);
}
} // namespace

CombatCore::CombatCore()
{
    Reset();
}

void CombatCore::Reset()
{
    fighters_ = {};
    auto& enemy = Mutable(FighterId::Enemy);
    enemy.health = enemy.maxHealth = 125.f;
    enemy.stance = Stance::Stone;
    enemy.decisionTime = 1.1f;
    enemySequence_ = 0;
    // Keep identifiers unique across restarts so engine-side hit/effect caches
    // cannot mistake the first attack of a rematch for an earlier attack.
}

FighterState& CombatCore::Mutable(FighterId id)
{
    return fighters_[static_cast<std::size_t>(id)];
}

const FighterState& CombatCore::Get(FighterId id) const
{
    return fighters_[static_cast<std::size_t>(id)];
}

FighterId CombatCore::Opponent(FighterId id)
{
    return id == FighterId::Player ? FighterId::Enemy : FighterId::Player;
}

void CombatCore::SetAction(FighterState& fighter, Action action, float duration)
{
    fighter.action = action;
    fighter.actionTime = 0.f;
    fighter.actionDuration = duration;
}

AttackSpec CombatCore::GetAttackSpec(Stance stance, AttackKind kind)
{
    if (kind == AttackKind::Unblockable)
        return {0.78f, 0.18f, 0.55f, 35.f, 35.f, 240.f, true};

    if (stance == Stance::Stone)
    {
        if (kind == AttackKind::Heavy)
            return {0.54f, 0.16f, 0.48f, 32.f, 48.f, 230.f, false};
        return {0.28f, 0.13f, 0.36f, 20.f, 25.f, 210.f, false};
    }
    if (stance == Stance::Gale)
    {
        if (kind == AttackKind::Heavy)
            return {0.33f, 0.15f, 0.31f, 21.f, 22.f, 245.f, false};
        return {0.14f, 0.12f, 0.21f, 12.f, 12.f, 225.f, false};
    }
    if (kind == AttackKind::Heavy)
        return {0.43f, 0.14f, 0.40f, 26.f, 30.f, 220.f, false};
    return {0.19f, 0.11f, 0.25f, 15.f, 18.f, 210.f, false};
}

bool CombatCore::TryAttack(FighterId id, AttackKind kind)
{
    auto& fighter = Mutable(id);
    if (Get(Opponent(id)).action == Action::Dead)
        return false;

    if (fighter.action != Action::Idle)
    {
        // Pressing again during the first windup does not turn a single input
        // burst into a whole combo. One fresh press during contact/follow-through
        // commits one subsequent cut; repeated presses cannot extend the queue.
        if (id != FighterId::Player || kind != AttackKind::Light ||
            fighter.currentAttack != AttackKind::Light || fighter.isCounterAttack ||
            fighter.comboStep >= LightComboLength - 1 || fighter.comboQueued ||
            (fighter.action != Action::Active && fighter.action != Action::Recovery))
            return false;
        if (fighter.action == Action::Recovery && fighter.actionTime >= ComboLinkRecovery)
            StartAttack(id, kind, fighter.comboStep + 1);
        else
            fighter.comboQueued = true;
        return true;
    }
    StartAttack(id, kind);
    return true;
}

void CombatCore::ClearChain(FighterState& fighter)
{
    fighter.comboQueued = false;
    fighter.comboStep = 0;
}

void CombatCore::ClearCounter(FighterState& fighter)
{
    fighter.counterTime = 0.f;
    fighter.counterSource = CounterSource::None;
}

void CombatCore::StartAttack(FighterId id, AttackKind kind, std::uint8_t comboStep)
{
    auto& fighter = Mutable(id);
    fighter.comboQueued = false;
    fighter.comboStep = comboStep;
    fighter.isCounterAttack = fighter.counterTime > 0.f;
    fighter.attackCounterSource = fighter.isCounterAttack ? fighter.counterSource : CounterSource::None;

    auto spec = GetAttackSpec(fighter.stance, kind);
    if (fighter.isCounterAttack)
    {
        spec.damage *= CounterDamageMultiplier;
        spec.postureDamage *= CounterPostureMultiplier;
        spec.windup *= CounterWindupMultiplier;
    }
    else if (comboStep == 1)
    {
        spec.damage *= 1.08f;
        spec.postureDamage *= 1.15f;
        spec.windup *= .9f;
    }
    else if (comboStep == 2)
    {
        spec.damage *= 1.04f;
        spec.postureDamage *= 1.1f;
        spec.windup *= .82f;
        spec.recovery *= .9f;
    }
    else if (comboStep == 3)
    {
        spec.damage *= 1.25f;
        spec.postureDamage *= 1.45f;
        spec.windup *= 1.08f;
        spec.recovery *= 1.05f;
    }
    else if (comboStep == 4)
    {
        spec.damage *= 1.55f;
        spec.postureDamage *= 1.85f;
        spec.windup *= 1.2f;
        spec.recovery *= 1.35f;
    }
    fighter.guarding = false;
    fighter.parryTime = 0.f;
    ClearCounter(fighter);
    fighter.currentAttack = kind;
    fighter.spec = spec;
    fighter.attackElapsed = 0.f;
    fighter.attackId = nextAttackId_++;
    fighter.hitResolved = false;
    fighter.lastHit = HitResult::None;
    SetAction(fighter, Action::Windup, spec.windup);
    if (id == FighterId::Enemy)
    {
        ++enemySequence_;
        fighter.decisionTime = spec.windup + spec.active + spec.recovery + 0.55f;
    }
}

void CombatCore::SetGuard(FighterId id, bool held, bool allowParry)
{
    auto& fighter = Mutable(id);
    if (!held)
    {
        fighter.guarding = false;
        fighter.parryTime = 0.f;
        return;
    }
    if (fighter.guarding || Get(Opponent(id)).action == Action::Dead ||
        (allowParry ? !CanDefend(fighter) : fighter.action != Action::Idle))
        return;
    // A new physical press interrupts a cancellable attack. Automatic restoration
    // of a held button cannot cancel an action or create a fresh parry window.
    if (fighter.action != Action::Idle)
    {
        fighter.hitResolved = true;
        SetAction(fighter, Action::Idle);
    }
    ClearChain(fighter);
    fighter.isCounterAttack = false;
    fighter.attackCounterSource = CounterSource::None;
    fighter.guarding = true;
    if (allowParry && fighter.parryCooldown <= Epsilon)
    {
        fighter.parryTime = ParryWindow;
        fighter.parryCooldown = ParryRetryDelay;
    }
}

bool CombatCore::TryDodge(FighterId id)
{
    auto& fighter = Mutable(id);
    if (!CanDefend(fighter) || Get(Opponent(id)).action == Action::Dead)
        return false;
    fighter.hitResolved = true;
    ClearChain(fighter);
    fighter.isCounterAttack = false;
    fighter.attackCounterSource = CounterSource::None;
    fighter.guarding = false;
    fighter.parryTime = 0.f;
    fighter.invulnerabilityTime = fighter.stance == Stance::Gale ? 0.15f : 0.12f;
    SetAction(fighter, Action::Dodging, fighter.stance == Stance::Gale ? 0.26f : 0.3f);
    return true;
}

bool CombatCore::SetStance(FighterId id, Stance stance)
{
    auto& fighter = Mutable(id);
    if (fighter.action != Action::Idle && fighter.action != Action::Dodging)
        return false;
    fighter.stance = stance;
    return true;
}

void CombatCore::Kill(FighterState& fighter)
{
    fighter.health = 0.f;
    fighter.guarding = false;
    fighter.parryTime = 0.f;
    fighter.invulnerabilityTime = 0.f;
    ClearCounter(fighter);
    ClearChain(fighter);
    fighter.isCounterAttack = false;
    fighter.attackCounterSource = CounterSource::None;
    fighter.hitResolved = true;
    SetAction(fighter, Action::Dead);
}

bool CombatCore::ApplyPosture(FighterState& fighter, float amount)
{
    fighter.posture = std::min(fighter.maxPosture, fighter.posture + amount);
    fighter.postureRecoveryDelay = 1.6f;
    if (fighter.action == Action::Broken)
        return false;
    if (fighter.posture + Epsilon < fighter.maxPosture)
        return false;
    fighter.guarding = false;
    fighter.parryTime = 0.f;
    fighter.invulnerabilityTime = 0.f;
    ClearCounter(fighter);
    ClearChain(fighter);
    fighter.isCounterAttack = false;
    fighter.attackCounterSource = CounterSource::None;
    fighter.hitResolved = true;
    SetAction(fighter, Action::Broken, GuardBreakDuration);
    return true;
}

HitResult CombatCore::ResolveHit(FighterId attackerId, float distance, bool facing)
{
    auto& attacker = Mutable(attackerId);
    auto& defender = Mutable(Opponent(attackerId));
    if (attacker.action != Action::Active || attacker.hitResolved || defender.action == Action::Dead)
        return HitResult::None;
    if (!std::isfinite(distance) || distance < 0.f || distance > attacker.spec.range || !facing)
        return HitResult::Miss;

    attacker.hitResolved = true;
    if (defender.invulnerabilityTime > 0.f)
    {
        // Reward evading a real incoming contact, not rolling outside its reach.
        defender.counterTime = std::max(defender.counterTime, DodgeCounterWindow);
        if (defender.counterSource == CounterSource::None)
            defender.counterSource = CounterSource::Dodge;
        return attacker.lastHit = HitResult::Dodged;
    }

    if (defender.guarding && defender.action == Action::Idle && !attacker.spec.unblockable)
    {
        if (defender.parryTime > 0.f)
        {
            defender.parryTime = 0.f;
            defender.parryCooldown = 0.f;
            defender.counterTime = ParryCounterWindow;
            defender.counterSource = CounterSource::Parry;
            defender.posture = std::max(0.f, defender.posture - 12.f);
            const float posture = defender.stance == Stance::Stone ? 42.f : 34.f;
            const bool broken = ApplyPosture(attacker, posture);
            if (!broken)
            {
                ClearChain(attacker);
                ClearCounter(attacker);
                attacker.isCounterAttack = false;
                attacker.attackCounterSource = CounterSource::None;
                SetAction(attacker, Action::Staggered, 0.58f);
            }
            attacker.decisionTime = 1.15f;
            return attacker.lastHit = broken ? HitResult::GuardBreak : HitResult::Parried;
        }

        const float guardFactor = defender.stance == Stance::Stone ? 0.72f : 1.f;
        const bool broken = ApplyPosture(defender, attacker.spec.postureDamage * guardFactor);
        return attacker.lastHit = broken ? HitResult::GuardBreak : HitResult::Blocked;
    }

    defender.health = std::max(0.f, defender.health - attacker.spec.damage);
    // Taking a hit loses the earned opening. A later normal swing must not
    // inherit an old parry's critical damage or a queued combo input.
    ClearCounter(defender);
    ClearChain(defender);
    defender.isCounterAttack = false;
    defender.attackCounterSource = CounterSource::None;
    if (defender.health <= 0.f)
    {
        Kill(defender);
        return attacker.lastHit = HitResult::Hit;
    }
    const bool alreadyBroken = defender.action == Action::Broken;
    const bool broken = ApplyPosture(defender, attacker.spec.postureDamage * 0.55f);
    if (!broken && !alreadyBroken)
    {
        defender.guarding = false;
        defender.parryTime = 0.f;
        defender.invulnerabilityTime = 0.f;
        defender.hitResolved = true;
        const float stagger = attacker.isCounterAttack ? .52f :
            (attacker.currentAttack == AttackKind::Light ? .28f : .42f);
        SetAction(defender, Action::Staggered, stagger);
    }
    return attacker.lastHit = broken ? HitResult::GuardBreak : HitResult::Hit;
}

bool CombatCore::TryExecute(FighterId attackerId, float distance, bool facing)
{
    auto& attacker = Mutable(attackerId);
    auto& defender = Mutable(Opponent(attackerId));
    if (attacker.action != Action::Idle || defender.action != Action::Broken || !facing ||
        !std::isfinite(distance) || distance < 0.f || distance > ExecutionRange)
        return false;
    Kill(defender);
    attacker.guarding = false;
    attacker.parryTime = 0.f;
    attacker.posture = 0.f;
    ClearCounter(attacker);
    ClearChain(attacker);
    attacker.isCounterAttack = false;
    attacker.attackCounterSource = CounterSource::None;
    SetAction(attacker, Action::Recovery, 0.6f);
    return true;
}

void CombatCore::TickFighter(FighterId id, float seconds)
{
    auto& fighter = Mutable(id);
    if (fighter.action == Action::Dead)
        return;

    // Segment at phase boundaries so regeneration is independent of the caller's
    // frame size, including a frame that spans recovery into idle.
    float remaining = seconds;
    while (remaining > Epsilon)
    {
        if (Get(Opponent(id)).action == Action::Dead)
            fighter.comboQueued = false;
        const bool linking = fighter.action == Action::Recovery && fighter.comboQueued;
        const bool timed = fighter.action != Action::Idle;
        float step = remaining;
        if (timed)
            step = std::min(step, std::max(0.f, fighter.actionDuration - fighter.actionTime));
        if (linking)
            step = std::min(step, std::max(0.f, ComboLinkRecovery - fighter.actionTime));

        const float postureTime = std::max(0.f, step - fighter.postureRecoveryDelay);
        if (fighter.action == Action::Idle && !fighter.guarding)
        {
            const float healthFactor = 0.35f + 0.65f * fighter.health / fighter.maxHealth;
            fighter.posture = std::max(0.f, fighter.posture - 16.f * healthFactor * postureTime);
        }
        CountDown(fighter.parryTime, step);
        CountDown(fighter.parryCooldown, step);
        CountDown(fighter.invulnerabilityTime, step);
        CountDown(fighter.counterTime, step);
        if (fighter.counterTime <= 0.f)
            fighter.counterSource = CounterSource::None;
        CountDown(fighter.decisionTime, step);
        CountDown(fighter.postureRecoveryDelay, step);
        if (IsAttacking(fighter.action))
            fighter.attackElapsed += step;
        fighter.actionTime += step;
        remaining -= step;

        if (linking && fighter.actionTime + Epsilon >= ComboLinkRecovery)
        {
            StartAttack(id, AttackKind::Light, fighter.comboStep + 1);
            continue;
        }

        if (!timed || fighter.actionTime + Epsilon < fighter.actionDuration)
            break;

        switch (fighter.action)
        {
        case Action::Windup: SetAction(fighter, Action::Active, fighter.spec.active); break;
        case Action::Active: SetAction(fighter, Action::Recovery, fighter.spec.recovery); break;
        case Action::Broken:
            fighter.posture = fighter.maxPosture * 0.3f;
            fighter.postureRecoveryDelay = 1.f;
            SetAction(fighter, Action::Idle);
            break;
        default:
            ClearChain(fighter);
            SetAction(fighter, Action::Idle);
            break;
        }
    }
}

void CombatCore::Tick(float deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
        return;
    TickFighter(FighterId::Player, deltaSeconds);
    TickFighter(FighterId::Enemy, deltaSeconds);
}

bool CombatCore::EnemyReady() const
{
    const auto& enemy = Get(FighterId::Enemy);
    return enemy.action == Action::Idle && enemy.decisionTime <= Epsilon &&
        Get(FighterId::Player).action != Action::Dead;
}

AttackKind CombatCore::NextEnemyAttack() const
{
    static constexpr std::array<AttackKind, 6> Pattern{
        AttackKind::Light, AttackKind::Light, AttackKind::Heavy,
        AttackKind::Unblockable, AttackKind::Heavy, AttackKind::Light
    };
    return Pattern[enemySequence_ % Pattern.size()];
}
} // namespace ronin
