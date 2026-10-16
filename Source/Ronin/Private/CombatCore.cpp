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
        return {0.78f, 0.18f, 0.55f, 35.f, 35.f, 32.f, 240.f, true};

    if (stance == Stance::Stone)
    {
        if (kind == AttackKind::Heavy)
            return {0.54f, 0.16f, 0.48f, 32.f, 48.f, 30.f, 230.f, false};
        return {0.28f, 0.13f, 0.36f, 20.f, 25.f, 18.f, 210.f, false};
    }
    if (stance == Stance::Gale)
    {
        if (kind == AttackKind::Heavy)
            return {0.33f, 0.15f, 0.31f, 21.f, 22.f, 21.f, 245.f, false};
        return {0.14f, 0.12f, 0.21f, 12.f, 12.f, 11.f, 225.f, false};
    }
    if (kind == AttackKind::Heavy)
        return {0.43f, 0.14f, 0.40f, 26.f, 30.f, 25.f, 220.f, false};
    return {0.19f, 0.11f, 0.25f, 15.f, 18.f, 14.f, 210.f, false};
}

bool CombatCore::TryAttack(FighterId id, AttackKind kind)
{
    auto& fighter = Mutable(id);
    if (fighter.action != Action::Idle || Get(Opponent(id)).action == Action::Dead)
        return false;

    auto spec = GetAttackSpec(fighter.stance, kind);
    if (fighter.stamina + Epsilon < spec.staminaCost)
        return false;
    if (fighter.counterTime > 0.f)
    {
        spec.damage *= 1.35f;
        spec.postureDamage *= 1.4f;
        spec.windup *= 0.75f;
    }
    fighter.stamina = std::max(0.f, fighter.stamina - spec.staminaCost);
    fighter.staminaRecoveryDelay = 0.65f;
    fighter.guarding = false;
    fighter.parryTime = 0.f;
    fighter.counterTime = 0.f;
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
    return true;
}

void CombatCore::SetGuard(FighterId id, bool held)
{
    auto& fighter = Mutable(id);
    if (!held)
    {
        fighter.guarding = false;
        fighter.parryTime = 0.f;
        return;
    }
    if (fighter.action != Action::Idle || fighter.guarding)
        return;
    fighter.guarding = true;
    if (fighter.parryCooldown <= Epsilon)
    {
        fighter.parryTime = ParryWindow;
        fighter.parryCooldown = 0.32f;
    }
}

bool CombatCore::TryDodge(FighterId id)
{
    auto& fighter = Mutable(id);
    const float cost = fighter.stance == Stance::Gale ? 18.f : 24.f;
    if (fighter.action != Action::Idle || fighter.stamina + Epsilon < cost)
        return false;
    fighter.stamina = std::max(0.f, fighter.stamina - cost);
    fighter.staminaRecoveryDelay = 0.55f;
    fighter.guarding = false;
    fighter.parryTime = 0.f;
    fighter.invulnerabilityTime = fighter.stance == Stance::Gale ? 0.26f : 0.22f;
    SetAction(fighter, Action::Dodging, fighter.stance == Stance::Gale ? 0.38f : 0.44f);
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
    fighter.counterTime = 0.f;
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
    fighter.counterTime = 0.f;
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
        return attacker.lastHit = HitResult::Dodged;

    if (defender.guarding && defender.action == Action::Idle && !attacker.spec.unblockable)
    {
        if (defender.parryTime > 0.f)
        {
            defender.parryTime = 0.f;
            defender.counterTime = 1.1f;
            defender.stamina = std::min(defender.maxStamina, defender.stamina + 12.f);
            defender.posture = std::max(0.f, defender.posture - 12.f);
            const float posture = defender.stance == Stance::Stone ? 42.f : 34.f;
            const bool broken = ApplyPosture(attacker, posture);
            if (!broken)
                SetAction(attacker, Action::Staggered, 0.58f);
            attacker.decisionTime = 1.15f;
            return attacker.lastHit = broken ? HitResult::GuardBreak : HitResult::Parried;
        }

        const float guardFactor = defender.stance == Stance::Stone ? 0.72f : 1.f;
        const float guardCost = attacker.spec.staminaCost * 0.65f;
        const bool exhausted = defender.stamina + Epsilon < guardCost;
        defender.stamina = std::max(0.f, defender.stamina - guardCost);
        defender.staminaRecoveryDelay = 0.65f;
        const bool broken = ApplyPosture(defender, exhausted ? defender.maxPosture : attacker.spec.postureDamage * guardFactor);
        return attacker.lastHit = broken ? HitResult::GuardBreak : HitResult::Blocked;
    }

    defender.health = std::max(0.f, defender.health - attacker.spec.damage);
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
        SetAction(defender, Action::Staggered, attacker.currentAttack == AttackKind::Light ? 0.28f : 0.42f);
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
    attacker.stamina = attacker.maxStamina;
    attacker.posture = 0.f;
    SetAction(attacker, Action::Recovery, 0.6f);
    return true;
}

void CombatCore::TickFighter(FighterState& fighter, float seconds)
{
    if (fighter.action == Action::Dead)
        return;

    // Segment at phase boundaries so regeneration is independent of the caller's
    // frame size, including a frame that spans recovery into idle.
    float remaining = seconds;
    while (remaining > Epsilon)
    {
        const bool timed = fighter.action != Action::Idle;
        float step = remaining;
        if (timed)
            step = std::min(step, std::max(0.f, fighter.actionDuration - fighter.actionTime));

        const float staminaTime = std::max(0.f, step - fighter.staminaRecoveryDelay);
        const float postureTime = std::max(0.f, step - fighter.postureRecoveryDelay);
        if (fighter.action == Action::Idle)
        {
            const float regen = fighter.guarding ? 12.f : 26.f;
            fighter.stamina = std::min(fighter.maxStamina, fighter.stamina + regen * staminaTime);
            const float healthFactor = 0.35f + 0.65f * fighter.health / fighter.maxHealth;
            fighter.posture = std::max(0.f, fighter.posture - 16.f * healthFactor * postureTime);
        }
        CountDown(fighter.parryTime, step);
        CountDown(fighter.parryCooldown, step);
        CountDown(fighter.invulnerabilityTime, step);
        CountDown(fighter.counterTime, step);
        CountDown(fighter.decisionTime, step);
        CountDown(fighter.postureRecoveryDelay, step);
        CountDown(fighter.staminaRecoveryDelay, step);
        if (IsAttacking(fighter.action))
            fighter.attackElapsed += step;
        fighter.actionTime += step;
        remaining -= step;

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
        default: SetAction(fighter, Action::Idle); break;
        }
    }
}

void CombatCore::Tick(float deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
        return;
    for (auto& fighter : fighters_)
        TickFighter(fighter, deltaSeconds);
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
