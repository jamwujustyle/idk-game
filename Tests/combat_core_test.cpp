#include "CombatCore.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace ronin;

namespace
{
int checks = 0;

void Check(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        std::exit(1);
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

bool Near(float left, float right, float tolerance = 0.002f)
{
    return std::fabs(left - right) < tolerance;
}

void AdvanceToIdle(CombatCore& combat, FighterId id)
{
    for (int frame = 0; combat.Get(id).action != Action::Idle && frame < 1000; ++frame)
        combat.Tick(1.f / 120.f);
    CHECK(combat.Get(id).action == Action::Idle);
}

void EnterActive(CombatCore& combat, FighterId id, AttackKind kind)
{
    CHECK(combat.TryAttack(id, kind));
    combat.Tick(combat.Get(id).spec.windup);
    CHECK(combat.Get(id).action == Action::Active);
}

void BreakEnemyWithParries(CombatCore& combat)
{
    for (int index = 0; index < 3; ++index)
    {
        CHECK(combat.TryAttack(FighterId::Enemy, AttackKind::Light));
        combat.Tick(combat.Get(FighterId::Enemy).spec.windup - 0.05f);
        combat.SetGuard(FighterId::Player, true);
        combat.Tick(0.05f);
        const auto result = combat.ResolveHit(FighterId::Enemy, 180.f);
        CHECK(result == (index == 2 ? HitResult::GuardBreak : HitResult::Parried));
        combat.SetGuard(FighterId::Player, false);
        if (index != 2)
            AdvanceToIdle(combat, FighterId::Enemy);
    }
    CHECK(combat.Get(FighterId::Enemy).action == Action::Broken);
}

void AttackPhasesAndContact()
{
    CombatCore combat;
    CHECK(combat.Get(FighterId::Player).health == 100.f);
    CHECK(combat.Get(FighterId::Enemy).health == 125.f);
    CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
    const auto spec = combat.Get(FighterId::Player).spec;
    CHECK(combat.ResolveHit(FighterId::Player, 100.f) == HitResult::None);
    CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Heavy));
    CHECK(!combat.SetStance(FighterId::Player, Stance::Stone));
    combat.Tick(spec.windup - 0.01f);
    CHECK(combat.Get(FighterId::Player).action == Action::Windup);
    combat.Tick(0.01f);
    CHECK(combat.Get(FighterId::Player).action == Action::Active);
    CHECK(!combat.TryDodge(FighterId::Player));
    combat.SetGuard(FighterId::Player, true);
    CHECK(!combat.Get(FighterId::Player).guarding);
    CHECK(combat.ResolveHit(FighterId::Player, spec.range + 1.f) == HitResult::Miss);
    CHECK(combat.ResolveHit(FighterId::Player, 100.f, false) == HitResult::Miss);
    CHECK(combat.ResolveHit(FighterId::Player, -1.f) == HitResult::Miss);
    CHECK(combat.ResolveHit(FighterId::Player, std::numeric_limits<float>::quiet_NaN()) == HitResult::Miss);
    CHECK(!combat.Get(FighterId::Player).hitResolved);
    CHECK(combat.ResolveHit(FighterId::Player, spec.range) == HitResult::Hit);
    CHECK(Near(combat.Get(FighterId::Enemy).health, 125.f - spec.damage));
    CHECK(combat.ResolveHit(FighterId::Player, 100.f) == HitResult::None);
    CHECK(combat.Get(FighterId::Enemy).action == Action::Staggered);
    combat.Tick(spec.active);
    CHECK(combat.Get(FighterId::Player).action == Action::Recovery);
    combat.Tick(spec.recovery);
    CHECK(combat.Get(FighterId::Player).action == Action::Idle);
}

void TimingIsFrameIndependent()
{
    CombatCore coarse;
    CombatCore fine;
    CHECK(coarse.TryAttack(FighterId::Player, AttackKind::Heavy));
    CHECK(fine.TryAttack(FighterId::Player, AttackKind::Heavy));
    coarse.Tick(2.f);
    for (int frame = 0; frame < 240; ++frame)
        fine.Tick(1.f / 120.f);
    const auto& a = coarse.Get(FighterId::Player);
    const auto& b = fine.Get(FighterId::Player);
    CHECK(a.action == Action::Idle && b.action == Action::Idle);
    CHECK(Near(a.attackElapsed, b.attackElapsed));
    CHECK(Near(a.actionTime, b.actionTime));
    CHECK(coarse.EnemyReady() && fine.EnemyReady());
    const float elapsed = a.actionTime;
    coarse.Tick(-1.f);
    coarse.Tick(std::numeric_limits<float>::infinity());
    coarse.Tick(std::numeric_limits<float>::quiet_NaN());
    CHECK(coarse.Get(FighterId::Player).actionTime == elapsed);

    for (auto* combat : {&coarse, &fine})
    {
        combat->SetGuard(FighterId::Player, true, false);
        EnterActive(*combat, FighterId::Enemy, AttackKind::Light);
        CHECK(combat->ResolveHit(FighterId::Enemy, 150.f) == HitResult::Blocked);
        combat->SetGuard(FighterId::Player, false);
    }
    coarse.Tick(2.f);
    for (int frame = 0; frame < 240; ++frame) fine.Tick(1.f / 120.f);
    CHECK(Near(coarse.Get(FighterId::Player).posture, fine.Get(FighterId::Player).posture));
    CHECK(Near(coarse.Get(FighterId::Player).posture, 18.6f));
}

void ParryAndCounter()
{
    CombatCore combat;
    CHECK(combat.TryAttack(FighterId::Enemy, AttackKind::Light));
    combat.Tick(combat.Get(FighterId::Enemy).spec.windup - 0.05f);
    combat.SetGuard(FighterId::Player, true);
    combat.Tick(0.05f);
    CHECK(combat.ResolveHit(FighterId::Enemy, 180.f) == HitResult::Parried);
    CHECK(combat.Get(FighterId::Enemy).action == Action::Staggered);
    CHECK(Near(combat.Get(FighterId::Enemy).posture, 34.f));
    CHECK(combat.Get(FighterId::Player).health == 100.f);
    CHECK(combat.Get(FighterId::Player).counterTime > 0.f);
    CHECK(combat.Get(FighterId::Player).counterSource == CounterSource::Parry);
    CHECK(combat.Get(FighterId::Player).parryCooldown == 0.f);
    combat.SetGuard(FighterId::Player, true);
    CHECK(combat.Get(FighterId::Player).parryTime == 0.f); // Holding cannot auto-deflect.
    combat.SetGuard(FighterId::Player, false);
    combat.SetGuard(FighterId::Player, true);
    CHECK(Near(combat.Get(FighterId::Player).parryTime, CombatCore::ParryWindow));
    CHECK(combat.SetStance(FighterId::Player, Stance::Stone));
    const auto base = CombatCore::GetAttackSpec(Stance::Stone, AttackKind::Heavy);
    CHECK(combat.TryAttack(FighterId::Player, AttackKind::Heavy));
    CHECK(Near(combat.Get(FighterId::Player).spec.damage, base.damage * CombatCore::CounterDamageMultiplier));
    CHECK(Near(combat.Get(FighterId::Player).spec.postureDamage, base.postureDamage * CombatCore::CounterPostureMultiplier));
    CHECK(Near(combat.Get(FighterId::Player).spec.windup, base.windup * CombatCore::CounterWindupMultiplier));
    CHECK(combat.Get(FighterId::Player).counterTime == 0.f);
    CHECK(combat.Get(FighterId::Player).counterSource == CounterSource::None);
    CHECK(combat.Get(FighterId::Player).isCounterAttack);
    CHECK(combat.Get(FighterId::Player).attackCounterSource == CounterSource::Parry);
    CHECK(!combat.Get(FighterId::Player).guarding);
}

void GuardHoldAndSpam()
{
    CombatCore combat;
    combat.SetGuard(FighterId::Player, true);
    combat.Tick(0.1f);
    const float remaining = combat.Get(FighterId::Player).parryTime;
    combat.SetGuard(FighterId::Player, true);
    CHECK(Near(combat.Get(FighterId::Player).parryTime, remaining));
    combat.SetGuard(FighterId::Player, false);
    combat.SetGuard(FighterId::Player, true);
    CHECK(combat.Get(FighterId::Player).parryTime == 0.f);
    CHECK(combat.Get(FighterId::Player).guarding);
    EnterActive(combat, FighterId::Enemy, AttackKind::Light);
    CHECK(combat.ResolveHit(FighterId::Enemy, 180.f) == HitResult::Blocked);
    CHECK(combat.Get(FighterId::Player).health == 100.f);
    CHECK(Near(combat.Get(FighterId::Player).posture, 25.f));
    combat.SetGuard(FighterId::Player, false);
    combat.SetGuard(FighterId::Player, true);
    CHECK(Near(combat.Get(FighterId::Player).parryTime, CombatCore::ParryWindow));

    CombatCore stone;
    CHECK(stone.SetStance(FighterId::Player, Stance::Stone));
    stone.SetGuard(FighterId::Player, true);
    EnterActive(stone, FighterId::Enemy, AttackKind::Light);
    CHECK(stone.ResolveHit(FighterId::Enemy, 180.f) == HitResult::Blocked);
    CHECK(Near(stone.Get(FighterId::Player).posture, 25.f * 0.72f));
}

void InterruptedGuardResumesWithoutAutomaticParry()
{
    CombatCore combat;
    combat.SetGuard(FighterId::Player, true);
    CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(!combat.Get(FighterId::Player).guarding);
    combat.SetGuard(FighterId::Player, true, false);
    CHECK(!combat.Get(FighterId::Player).guarding);
    AdvanceToIdle(combat, FighterId::Player);
    CHECK(combat.Get(FighterId::Player).parryCooldown == 0.f);
    combat.SetGuard(FighterId::Player, true, false);
    CHECK(combat.Get(FighterId::Player).guarding);
    CHECK(combat.Get(FighterId::Player).parryTime == 0.f);
    CHECK(combat.Get(FighterId::Player).parryCooldown == 0.f);
    EnterActive(combat, FighterId::Enemy, AttackKind::Light);
    CHECK(combat.ResolveHit(FighterId::Enemy, 180.f) == HitResult::Blocked);

    CombatCore cooldown;
    cooldown.SetGuard(FighterId::Player, true);
    const float remaining = cooldown.Get(FighterId::Player).parryCooldown;
    cooldown.SetGuard(FighterId::Player, false);
    CHECK(cooldown.Get(FighterId::Player).parryTime == 0.f);
    cooldown.SetGuard(FighterId::Player, true, false);
    CHECK(cooldown.Get(FighterId::Player).guarding);
    CHECK(cooldown.Get(FighterId::Player).parryTime == 0.f);
    CHECK(cooldown.Get(FighterId::Player).parryCooldown == remaining);
    cooldown.Tick(0.33f);
    cooldown.SetGuard(FighterId::Player, true, false);
    CHECK(cooldown.Get(FighterId::Player).parryTime == 0.f);
    CHECK(cooldown.Get(FighterId::Player).parryCooldown == 0.f);
    cooldown.SetGuard(FighterId::Player, false);
    cooldown.SetGuard(FighterId::Player, true);
    CHECK(Near(cooldown.Get(FighterId::Player).parryTime, CombatCore::ParryWindow));
}

void DodgeTimingAndUnblockable()
{
    CombatCore timed;
    CHECK(timed.TryAttack(FighterId::Enemy, AttackKind::Light));
    timed.Tick(timed.Get(FighterId::Enemy).spec.windup - 0.04f);
    CHECK(timed.TryDodge(FighterId::Player));
    CHECK(timed.SetStance(FighterId::Player, Stance::Stone));
    timed.Tick(0.04f);
    CHECK(timed.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Dodged);
    CHECK(timed.Get(FighterId::Player).health == 100.f);
    CHECK(Near(timed.Get(FighterId::Player).counterTime, CombatCore::DodgeCounterWindow));
    CHECK(timed.Get(FighterId::Player).counterSource == CounterSource::Dodge);
    CHECK(timed.ResolveHit(FighterId::Enemy, 150.f) == HitResult::None);
    CHECK(!timed.TryDodge(FighterId::Player));
    AdvanceToIdle(timed, FighterId::Player);
    CHECK(timed.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(Near(timed.Get(FighterId::Player).spec.windup,
        CombatCore::GetAttackSpec(Stance::Stone, AttackKind::Light).windup * CombatCore::CounterWindupMultiplier));
    CHECK(timed.Get(FighterId::Player).isCounterAttack);
    CHECK(timed.Get(FighterId::Player).attackCounterSource == CounterSource::Dodge);

    CombatCore early;
    CHECK(early.TryDodge(FighterId::Player));
    EnterActive(early, FighterId::Enemy, AttackKind::Light);
    CHECK(early.Get(FighterId::Player).action == Action::Dodging);
    CHECK(early.Get(FighterId::Player).invulnerabilityTime == 0.f);
    CHECK(early.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Hit);
    CHECK(early.Get(FighterId::Player).health == 80.f);

    CombatCore guard;
    CHECK(guard.TryAttack(FighterId::Enemy, AttackKind::Unblockable));
    guard.Tick(guard.Get(FighterId::Enemy).spec.windup - 0.04f);
    guard.SetGuard(FighterId::Player, true);
    guard.Tick(0.04f);
    CHECK(guard.Get(FighterId::Player).parryTime > 0.f);
    CHECK(guard.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Hit);
    CHECK(guard.Get(FighterId::Player).health == 65.f);

    CombatCore dodge;
    CHECK(dodge.TryAttack(FighterId::Enemy, AttackKind::Unblockable));
    dodge.Tick(dodge.Get(FighterId::Enemy).spec.windup - 0.04f);
    CHECK(dodge.TryDodge(FighterId::Player));
    dodge.Tick(0.04f);
    CHECK(dodge.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Dodged);
}

void GuardBreakAndExecution()
{
    CombatCore combat;
    CHECK(!combat.TryExecute(FighterId::Player, 100.f));
    BreakEnemyWithParries(combat);
    CHECK(Near(combat.Get(FighterId::Enemy).posture, 100.f));
    CHECK(!combat.EnemyReady());
    CHECK(!combat.TryAttack(FighterId::Enemy, AttackKind::Light));
    CHECK(!combat.TryExecute(FighterId::Player, CombatCore::ExecutionRange + 1.f));
    CHECK(!combat.TryExecute(FighterId::Player, 100.f, false));
    CHECK(!combat.TryExecute(FighterId::Player, -1.f));
    CHECK(!combat.TryExecute(FighterId::Player, std::numeric_limits<float>::quiet_NaN()));
    CHECK(combat.TryExecute(FighterId::Player, CombatCore::ExecutionRange));
    CHECK(combat.Get(FighterId::Enemy).action == Action::Dead);
    CHECK(combat.Get(FighterId::Enemy).health == 0.f);
    CHECK(combat.Get(FighterId::Player).action == Action::Recovery);
    CHECK(combat.Get(FighterId::Player).posture == 0.f);
    combat.Tick(5.f);
    CHECK(!combat.EnemyReady());
    CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(!combat.TryDodge(FighterId::Enemy));

    CombatCore expired;
    BreakEnemyWithParries(expired);
    expired.Tick(CombatCore::GuardBreakDuration);
    CHECK(expired.Get(FighterId::Enemy).action == Action::Idle);
    CHECK(Near(expired.Get(FighterId::Enemy).posture, 30.f));
    CHECK(!expired.TryExecute(FighterId::Player, 100.f));

    CombatCore blocking;
    blocking.SetGuard(FighterId::Player, true);
    for (int strike = 0; strike < 3; ++strike)
    {
        EnterActive(blocking, FighterId::Enemy, AttackKind::Heavy);
        CHECK(blocking.ResolveHit(FighterId::Enemy, 180.f) ==
            (strike == 2 ? HitResult::GuardBreak : HitResult::Blocked));
        if (strike != 2)
            AdvanceToIdle(blocking, FighterId::Enemy);
    }
    CHECK(blocking.Get(FighterId::Player).action == Action::Broken);
    CHECK(blocking.Get(FighterId::Player).health == 100.f);
    blocking.SetGuard(FighterId::Player, true);
    CHECK(!blocking.Get(FighterId::Player).guarding);
}

void StancesAndUnrestrictedActionUse()
{
    const auto flow = CombatCore::GetAttackSpec(Stance::Flow, AttackKind::Light);
    const auto stone = CombatCore::GetAttackSpec(Stance::Stone, AttackKind::Heavy);
    const auto gale = CombatCore::GetAttackSpec(Stance::Gale, AttackKind::Light);
    CHECK(gale.windup < flow.windup && gale.damage < flow.damage);
    CHECK(stone.postureDamage > flow.postureDamage && stone.recovery > flow.recovery);

    CombatCore combat;
    CHECK(combat.SetStance(FighterId::Player, Stance::Gale));
    CHECK(combat.TryDodge(FighterId::Player));
    CHECK(Near(combat.Get(FighterId::Player).invulnerabilityTime, 0.15f));
    CHECK(Near(combat.Get(FighterId::Player).actionDuration, 0.26f));
    combat.Reset();
    for (int dodge = 0; dodge < 20; ++dodge)
    {
        CHECK(combat.TryDodge(FighterId::Player));
        combat.Tick(.12f);
        CHECK(combat.Get(FighterId::Player).invulnerabilityTime == 0.f);
        CHECK(!combat.TryDodge(FighterId::Player));
        combat.SetGuard(FighterId::Player, true);
        CHECK(!combat.Get(FighterId::Player).guarding);
        combat.Tick(.18f);
        CHECK(combat.Get(FighterId::Player).action == Action::Idle);
    }
    for (int attack = 0; attack < 20; ++attack)
    {
        CHECK(combat.TryAttack(FighterId::Player, AttackKind::Heavy));
        const auto& spec = combat.Get(FighterId::Player).spec;
        combat.Tick(spec.windup + spec.active + spec.recovery);
        CHECK(combat.Get(FighterId::Player).action == Action::Idle);
    }
    CHECK(combat.Get(FighterId::Player).posture == 0.f);
}

void DefenseCancelsAndCommitment()
{
    CombatCore guard;
    CHECK(guard.TryAttack(FighterId::Player, AttackKind::Heavy));
    guard.Tick(.1f);
    const auto canceledId = guard.Get(FighterId::Player).attackId;
    guard.SetGuard(FighterId::Player, true);
    CHECK(guard.Get(FighterId::Player).action == Action::Idle);
    CHECK(guard.Get(FighterId::Player).guarding);
    CHECK(guard.Get(FighterId::Player).parryTime == CombatCore::ParryWindow);
    CHECK(guard.Get(FighterId::Player).actionTime == 0.f);
    CHECK(guard.Get(FighterId::Player).hitResolved);
    guard.Tick(.5f);
    CHECK(guard.ResolveHit(FighterId::Player, 100.f) == HitResult::None);
    CHECK(guard.Get(FighterId::Enemy).health == 125.f);
    CHECK(guard.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(guard.Get(FighterId::Player).attackId > canceledId);
    CHECK(!guard.Get(FighterId::Player).hitResolved);
    CHECK(guard.Get(FighterId::Player).attackElapsed == 0.f);

    CombatCore dodge;
    CHECK(dodge.TryAttack(FighterId::Player, AttackKind::Heavy));
    dodge.Tick(.1f);
    CHECK(dodge.TryDodge(FighterId::Player));
    CHECK(dodge.Get(FighterId::Player).action == Action::Dodging);
    CHECK(dodge.Get(FighterId::Player).actionTime == 0.f);
    CHECK(dodge.ResolveHit(FighterId::Player, 100.f) == HitResult::None);

    CombatCore strike;
    EnterActive(strike, FighterId::Player, AttackKind::Light);
    CHECK(strike.ResolveHit(FighterId::Player, 1000.f) == HitResult::Miss);
    CHECK(!strike.TryDodge(FighterId::Player));
    CHECK(strike.ResolveHit(FighterId::Player, 150.f) == HitResult::Hit);
    CHECK(strike.TryDodge(FighterId::Player)); // Resolved contact permits a defensive exit.
    CHECK(strike.ResolveHit(FighterId::Player, 150.f) == HitResult::None);
    CHECK(!strike.TryDodge(FighterId::Enemy)); // A hit stun cannot be escaped.
    strike.SetGuard(FighterId::Enemy, true);
    CHECK(!strike.Get(FighterId::Enemy).guarding);

    CombatCore recovery;
    EnterActive(recovery, FighterId::Player, AttackKind::Light);
    recovery.Tick(recovery.Get(FighterId::Player).spec.active);
    CHECK(recovery.Get(FighterId::Player).action == Action::Recovery);
    recovery.SetGuard(FighterId::Player, true, false);
    CHECK(recovery.Get(FighterId::Player).action == Action::Recovery);
    recovery.SetGuard(FighterId::Player, true);
    CHECK(recovery.Get(FighterId::Player).action == Action::Idle);
    CHECK(recovery.Get(FighterId::Player).guarding);

    CombatCore broken;
    BreakEnemyWithParries(broken);
    CHECK(!broken.TryDodge(FighterId::Enemy));
    broken.SetGuard(FighterId::Enemy, true);
    CHECK(!broken.Get(FighterId::Enemy).guarding);
}

void DefenseBoundariesAndPosturePressure()
{
    for (const bool inside : {true, false})
    {
        CombatCore parry;
        CHECK(parry.TryAttack(FighterId::Enemy, AttackKind::Heavy));
        const float time = CombatCore::ParryWindow - (inside ? .001f : 0.f);
        parry.Tick(parry.Get(FighterId::Enemy).spec.windup - time);
        parry.SetGuard(FighterId::Player, true);
        parry.Tick(time);
        CHECK(parry.ResolveHit(FighterId::Enemy, 150.f) ==
            (inside ? HitResult::Parried : HitResult::Blocked));

        CombatCore dodge;
        CHECK(dodge.TryAttack(FighterId::Enemy, AttackKind::Heavy));
        const float evadeTime = .12f - (inside ? .001f : 0.f);
        dodge.Tick(dodge.Get(FighterId::Enemy).spec.windup - evadeTime);
        CHECK(dodge.TryDodge(FighterId::Player));
        dodge.Tick(evadeTime);
        CHECK(dodge.ResolveHit(FighterId::Enemy, 150.f) ==
            (inside ? HitResult::Dodged : HitResult::Hit));
    }

    CombatCore miss;
    EnterActive(miss, FighterId::Enemy, AttackKind::Light);
    CHECK(miss.TryDodge(FighterId::Player));
    CHECK(miss.ResolveHit(FighterId::Enemy, 1000.f) == HitResult::Miss);
    CHECK(miss.Get(FighterId::Player).counterTime == 0.f);

    CombatCore guard;
    guard.SetGuard(FighterId::Player, true, false);
    EnterActive(guard, FighterId::Enemy, AttackKind::Heavy);
    CHECK(guard.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Blocked);
    guard.Tick(5.f);
    CHECK(Near(guard.Get(FighterId::Player).posture, 48.f)); // Holding is not free posture recovery.
    guard.SetGuard(FighterId::Player, false);
    guard.Tick(.5f);
    CHECK(Near(guard.Get(FighterId::Player).posture, 40.f));
}

void EnemyRhythmResetAndLethality()
{
    CombatCore combat;
    CHECK(!combat.EnemyReady());
    combat.Tick(1.1f);
    CHECK(combat.EnemyReady());
    const AttackKind expected[] = {AttackKind::Light, AttackKind::Light, AttackKind::Heavy,
        AttackKind::Unblockable, AttackKind::Heavy, AttackKind::Light, AttackKind::Light};
    for (auto kind : expected)
    {
        CHECK(combat.NextEnemyAttack() == kind);
        CHECK(combat.TryAttack(FighterId::Enemy, kind));
        CHECK(!combat.EnemyReady());
        AdvanceToIdle(combat, FighterId::Enemy);
        CHECK(!combat.EnemyReady());
        combat.Tick(2.f);
        CHECK(combat.EnemyReady());
    }
    const auto oldId = combat.Get(FighterId::Enemy).attackId;
    combat.Reset();
    CHECK(combat.NextEnemyAttack() == AttackKind::Light);
    CHECK(combat.Get(FighterId::Player).health == 100.f);
    CHECK(combat.Get(FighterId::Enemy).posture == 0.f);
    CHECK(combat.Get(FighterId::Enemy).attackId == 0);
    CHECK(combat.TryAttack(FighterId::Enemy, AttackKind::Heavy));
    CHECK(combat.Get(FighterId::Enemy).attackId > oldId);

    combat.Reset();
    for (int strike = 0; strike < 4; ++strike)
    {
        EnterActive(combat, FighterId::Enemy, AttackKind::Unblockable);
        CHECK(combat.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Hit);
        if (strike == 2)
            break;
        combat.Tick(2.f);
    }
    CHECK(combat.Get(FighterId::Player).health == 0.f);
    CHECK(combat.Get(FighterId::Player).action == Action::Dead);
    CHECK(!combat.EnemyReady());
    combat.Reset();
    CHECK(combat.Get(FighterId::Player).action == Action::Idle);
    CHECK(combat.Get(FighterId::Player).health == 100.f);
}

void DeliberateLightChains()
{
    constexpr float Damage[] = {1.f, 1.08f, 1.04f, 1.25f, 1.55f};
    constexpr float Posture[] = {1.f, 1.15f, 1.1f, 1.45f, 1.85f};
    constexpr float Windup[] = {1.f, .9f, .82f, 1.08f, 1.2f};
    constexpr float Recovery[] = {1.f, 1.f, .9f, 1.05f, 1.35f};
    CHECK(CombatCore::LightComboLength == 5);
    for (const auto stance : {Stance::Flow, Stance::Stone, Stance::Gale})
    {
        CombatCore combat;
        CHECK(combat.SetStance(FighterId::Player, stance));
        CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
        const auto base = CombatCore::GetAttackSpec(stance, AttackKind::Light);
        float totalDamage = 0.f;
        std::uint64_t previousId = 0;
        for (std::uint8_t cut = 0; cut < CombatCore::LightComboLength; ++cut)
        {
            const auto state = combat.Get(FighterId::Player);
            CHECK(state.comboStep == cut);
            CHECK(state.action == Action::Windup);
            CHECK(state.attackId > previousId);
            CHECK(!state.comboQueued);
            CHECK(!state.isCounterAttack);
            CHECK(Near(state.attackElapsed, 0.f));
            CHECK(Near(state.spec.damage, base.damage * Damage[cut]));
            CHECK(Near(state.spec.postureDamage, base.postureDamage * Posture[cut]));
            CHECK(Near(state.spec.windup, base.windup * Windup[cut]));
            CHECK(Near(state.spec.active, base.active));
            CHECK(Near(state.spec.recovery, base.recovery * Recovery[cut]));
            CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Light));
            CHECK(!combat.Get(FighterId::Player).comboQueued);
            combat.Tick(state.spec.windup);
            CHECK(combat.ResolveHit(FighterId::Player, 150.f) == HitResult::Hit);
            CHECK(combat.ResolveHit(FighterId::Player, 150.f) == HitResult::None);
            totalDamage += state.spec.damage;
            CHECK(Near(combat.Get(FighterId::Enemy).health, 125.f - totalDamage));
            CHECK(Near(combat.Get(FighterId::Enemy).actionDuration, .28f));
            previousId = state.attackId;
            if (cut + 1 == CombatCore::LightComboLength)
            {
                CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Light));
                combat.Tick(state.spec.active);
                CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Light));
                combat.Tick(state.spec.recovery);
            }
            else if (cut % 2 == 0)
            {
                CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
                CHECK(combat.Get(FighterId::Player).comboQueued);
                CHECK(combat.Get(FighterId::Player).attackId == previousId);
                CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Light));
                CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Heavy));
                combat.Tick(state.spec.active + CombatCore::ComboLinkRecovery - .001f);
                CHECK(combat.Get(FighterId::Player).attackId == previousId);
                CHECK(combat.Get(FighterId::Player).action == Action::Recovery);
                combat.Tick(.001f);
            }
            else
            {
                // Late recovery presses link immediately without restarting or
                // extending the prior strike. They still require a fresh press.
                combat.Tick(state.spec.active + CombatCore::ComboLinkRecovery + .01f);
                CHECK(combat.Get(FighterId::Player).action == Action::Recovery);
                CHECK(combat.Get(FighterId::Player).attackId == previousId);
                CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
            }
        }
        CHECK(combat.Get(FighterId::Player).action == Action::Idle);
        CHECK(combat.Get(FighterId::Player).comboStep == 0);
        CHECK(!combat.Get(FighterId::Player).comboQueued);
        CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
        CHECK(combat.Get(FighterId::Player).comboStep == 0);
        CHECK(Near(combat.Get(FighterId::Player).spec.damage, base.damage));
    }
}

void QueuedChainsCancelAndDoNotAutoRepeat()
{
    // Every non-finishing cut offers the same defensive exit. Earlier cuts
    // deliberately whiff so this test does not confuse death with cancellation.
    for (std::uint8_t cut = 0; cut + 1 < CombatCore::LightComboLength; ++cut)
    {
        for (const bool dodge : {false, true})
        {
            CombatCore combat;
            CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
            for (std::uint8_t earlier = 0; earlier < cut; ++earlier)
            {
                combat.Tick(combat.Get(FighterId::Player).spec.windup);
                CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
                combat.Tick(combat.Get(FighterId::Player).spec.active + CombatCore::ComboLinkRecovery);
            }
            combat.Tick(combat.Get(FighterId::Player).spec.windup);
            CHECK(combat.Get(FighterId::Player).comboStep == cut);
            CHECK(combat.ResolveHit(FighterId::Player, 150.f) == HitResult::Hit);
            CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
            const auto oldId = combat.Get(FighterId::Player).attackId;
            if (dodge) CHECK(combat.TryDodge(FighterId::Player));
            else combat.SetGuard(FighterId::Player, true);
            CHECK(!combat.Get(FighterId::Player).comboQueued);
            combat.Tick(2.f);
            CHECK(combat.Get(FighterId::Player).attackId == oldId);
            CHECK(combat.Get(FighterId::Player).action == Action::Idle);
            CHECK(combat.Get(FighterId::Player).comboStep == 0);
        }
    }

    CombatCore hit;
    EnterActive(hit, FighterId::Player, AttackKind::Light);
    CHECK(hit.TryAttack(FighterId::Player, AttackKind::Light));
    EnterActive(hit, FighterId::Enemy, AttackKind::Light);
    const auto interruptedId = hit.Get(FighterId::Player).attackId;
    CHECK(hit.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Hit);
    CHECK(!hit.Get(FighterId::Player).comboQueued);
    hit.Tick(2.f);
    CHECK(hit.Get(FighterId::Player).attackId == interruptedId);

    CombatCore parried;
    EnterActive(parried, FighterId::Player, AttackKind::Light);
    CHECK(parried.TryAttack(FighterId::Player, AttackKind::Light));
    const auto parriedId = parried.Get(FighterId::Player).attackId;
    parried.SetGuard(FighterId::Enemy, true);
    CHECK(parried.ResolveHit(FighterId::Player, 150.f) == HitResult::Parried);
    CHECK(!parried.Get(FighterId::Player).comboQueued);
    parried.Tick(2.f);
    CHECK(parried.Get(FighterId::Player).attackId == parriedId);

    CombatCore oneFollowup;
    EnterActive(oneFollowup, FighterId::Player, AttackKind::Light);
    CHECK(oneFollowup.TryAttack(FighterId::Player, AttackKind::Light));
    const auto oldId = oneFollowup.Get(FighterId::Player).attackId;
    oneFollowup.Tick(3.f);
    CHECK(oneFollowup.Get(FighterId::Player).attackId == oldId + 1);
    CHECK(oneFollowup.Get(FighterId::Player).action == Action::Idle);

    CombatCore dead;
    for (int strike = 0; strike < 4; ++strike)
    {
        EnterActive(dead, FighterId::Player, AttackKind::Heavy);
        CHECK(dead.ResolveHit(FighterId::Player, 150.f) == HitResult::Hit);
        dead.Tick(2.f);
    }
    EnterActive(dead, FighterId::Player, AttackKind::Light);
    CHECK(dead.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(dead.ResolveHit(FighterId::Player, 150.f) == HitResult::Hit);
    dead.Tick(dead.Get(FighterId::Player).spec.active + CombatCore::ComboLinkRecovery);
    CHECK(dead.Get(FighterId::Player).comboStep == 1);
    dead.Tick(dead.Get(FighterId::Player).spec.windup);
    CHECK(dead.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(dead.ResolveHit(FighterId::Player, 150.f) == HitResult::Hit);
    // The third strike was queued, but the second killed its target.
    CHECK(dead.Get(FighterId::Enemy).action == Action::Dead);
    const auto killId = dead.Get(FighterId::Player).attackId;
    dead.Tick(3.f);
    CHECK(dead.Get(FighterId::Player).attackId == killId);
    CHECK(!dead.Get(FighterId::Player).comboQueued);
}

void QueuedTimingIsFrameIndependent()
{
    CombatCore coarse;
    CombatCore fine;
    for (auto* combat : {&coarse, &fine})
    {
        EnterActive(*combat, FighterId::Player, AttackKind::Light);
        CHECK(combat->TryAttack(FighterId::Player, AttackKind::Light));
    }
    coarse.Tick(.35f);
    for (int frame = 0; frame < 42; ++frame) fine.Tick(1.f / 120.f);
    const auto& a = coarse.Get(FighterId::Player);
    const auto& b = fine.Get(FighterId::Player);
    CHECK(a.action == b.action);
    CHECK(a.attackId == b.attackId);
    CHECK(a.comboStep == b.comboStep && a.comboStep == 1);
    CHECK(Near(a.attackElapsed, b.attackElapsed));
    CHECK(Near(a.actionTime, b.actionTime));
    CHECK(a.comboQueued == b.comboQueued && !a.comboQueued);
}

void CriticalOpeningIsEarnedAndConsumedOnce()
{
    CombatCore combat;
    CHECK(combat.TryAttack(FighterId::Enemy, AttackKind::Light));
    combat.Tick(combat.Get(FighterId::Enemy).spec.windup - .05f);
    combat.SetGuard(FighterId::Player, true);
    combat.Tick(.05f);
    CHECK(combat.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Parried);
    CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
    const auto critical = combat.Get(FighterId::Player).spec;
    CHECK(combat.Get(FighterId::Player).isCounterAttack);
    combat.Tick(critical.windup);
    CHECK(combat.Get(FighterId::Enemy).action == Action::Staggered);
    CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(combat.ResolveHit(FighterId::Player, 150.f) == HitResult::Hit);
    CHECK(Near(combat.Get(FighterId::Enemy).health, 125.f - critical.damage));
    CHECK(Near(combat.Get(FighterId::Enemy).actionDuration, .52f));
    CHECK(combat.ResolveHit(FighterId::Player, 150.f) == HitResult::None);
    combat.Tick(critical.active + critical.recovery);
    CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(!combat.Get(FighterId::Player).isCounterAttack);
    CHECK(combat.Get(FighterId::Player).attackCounterSource == CounterSource::None);
    CHECK(Near(combat.Get(FighterId::Player).spec.damage,
        CombatCore::GetAttackSpec(Stance::Flow, AttackKind::Light).damage));

    CombatCore expired;
    CHECK(expired.TryAttack(FighterId::Enemy, AttackKind::Light));
    expired.Tick(expired.Get(FighterId::Enemy).spec.windup - .05f);
    expired.SetGuard(FighterId::Player, true);
    expired.Tick(.05f);
    CHECK(expired.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Parried);
    expired.Tick(CombatCore::ParryCounterWindow);
    CHECK(expired.Get(FighterId::Player).counterSource == CounterSource::None);
    CHECK(expired.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(!expired.Get(FighterId::Player).isCounterAttack);

    CombatCore wounded;
    CHECK(wounded.TryAttack(FighterId::Enemy, AttackKind::Light));
    wounded.Tick(wounded.Get(FighterId::Enemy).spec.windup - .05f);
    wounded.SetGuard(FighterId::Player, true);
    wounded.Tick(.05f);
    CHECK(wounded.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Parried);
    wounded.SetGuard(FighterId::Player, false);
    wounded.Tick(.58f);
    EnterActive(wounded, FighterId::Enemy, AttackKind::Light);
    CHECK(wounded.Get(FighterId::Player).counterTime > 0.f);
    CHECK(wounded.ResolveHit(FighterId::Enemy, 150.f) == HitResult::Hit);
    CHECK(wounded.Get(FighterId::Player).counterTime == 0.f);
    CHECK(wounded.Get(FighterId::Player).counterSource == CounterSource::None);
    AdvanceToIdle(wounded, FighterId::Player);
    CHECK(wounded.TryAttack(FighterId::Player, AttackKind::Light));
    CHECK(!wounded.Get(FighterId::Player).isCounterAttack);
}
} // namespace

int main()
{
    AttackPhasesAndContact();
    TimingIsFrameIndependent();
    ParryAndCounter();
    GuardHoldAndSpam();
    InterruptedGuardResumesWithoutAutomaticParry();
    DodgeTimingAndUnblockable();
    GuardBreakAndExecution();
    StancesAndUnrestrictedActionUse();
    DefenseCancelsAndCommitment();
    DefenseBoundariesAndPosturePressure();
    EnemyRhythmResetAndLethality();
    DeliberateLightChains();
    QueuedChainsCancelAndDoNotAutoRepeat();
    QueuedTimingIsFrameIndependent();
    CriticalOpeningIsEarnedAndConsumedOnce();
    std::cout << "Combat core: " << checks << " checks passed.\n";
}
