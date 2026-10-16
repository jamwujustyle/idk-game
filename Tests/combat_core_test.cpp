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
    CHECK(!combat.TryDodge(FighterId::Player));
    CHECK(!combat.SetStance(FighterId::Player, Stance::Stone));
    combat.Tick(spec.windup - 0.01f);
    CHECK(combat.Get(FighterId::Player).action == Action::Windup);
    combat.Tick(0.01f);
    CHECK(combat.Get(FighterId::Player).action == Action::Active);
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
    CHECK(Near(a.stamina, b.stamina));
    CHECK(Near(a.attackElapsed, b.attackElapsed));
    CHECK(Near(a.actionTime, b.actionTime));
    CHECK(coarse.EnemyReady() && fine.EnemyReady());
    const float stamina = a.stamina;
    coarse.Tick(-1.f);
    coarse.Tick(std::numeric_limits<float>::infinity());
    coarse.Tick(std::numeric_limits<float>::quiet_NaN());
    CHECK(coarse.Get(FighterId::Player).stamina == stamina);
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
    CHECK(combat.SetStance(FighterId::Player, Stance::Stone));
    const auto base = CombatCore::GetAttackSpec(Stance::Stone, AttackKind::Heavy);
    CHECK(combat.TryAttack(FighterId::Player, AttackKind::Heavy));
    CHECK(Near(combat.Get(FighterId::Player).spec.damage, base.damage * 1.35f));
    CHECK(Near(combat.Get(FighterId::Player).spec.postureDamage, base.postureDamage * 1.4f));
    CHECK(Near(combat.Get(FighterId::Player).spec.windup, base.windup * 0.75f));
    CHECK(combat.Get(FighterId::Player).counterTime == 0.f);
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
    CHECK(combat.Get(FighterId::Player).stamina < 100.f);
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
    CHECK(!timed.TryDodge(FighterId::Player));

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
    CHECK(combat.Get(FighterId::Player).stamina == 100.f);
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

void StanceCostsAndStamina()
{
    const auto flow = CombatCore::GetAttackSpec(Stance::Flow, AttackKind::Light);
    const auto stone = CombatCore::GetAttackSpec(Stance::Stone, AttackKind::Heavy);
    const auto gale = CombatCore::GetAttackSpec(Stance::Gale, AttackKind::Light);
    CHECK(gale.windup < flow.windup && gale.damage < flow.damage);
    CHECK(stone.postureDamage > flow.postureDamage && stone.recovery > flow.recovery);

    CombatCore combat;
    CHECK(combat.SetStance(FighterId::Player, Stance::Gale));
    CHECK(combat.TryDodge(FighterId::Player));
    CHECK(Near(combat.Get(FighterId::Player).stamina, 82.f));
    CHECK(Near(combat.Get(FighterId::Player).invulnerabilityTime, 0.26f));
    CHECK(Near(combat.Get(FighterId::Player).actionDuration, 0.38f));
    combat.Reset();
    for (int dodge = 0; dodge < 4; ++dodge)
    {
        CHECK(combat.TryDodge(FighterId::Player));
        combat.Tick(0.44f);
    }
    CHECK(Near(combat.Get(FighterId::Player).stamina, 4.f));
    CHECK(!combat.TryDodge(FighterId::Player));
    CHECK(!combat.TryAttack(FighterId::Player, AttackKind::Light));
    combat.Tick(1.f);
    CHECK(combat.TryAttack(FighterId::Player, AttackKind::Light));
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
} // namespace

int main()
{
    AttackPhasesAndContact();
    TimingIsFrameIndependent();
    ParryAndCounter();
    GuardHoldAndSpam();
    DodgeTimingAndUnblockable();
    GuardBreakAndExecution();
    StanceCostsAndStamina();
    EnemyRhythmResetAndLethality();
    std::cout << "Combat core: " << checks << " checks passed.\n";
}
