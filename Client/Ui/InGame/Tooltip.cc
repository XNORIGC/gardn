#include <Client/Ui/InGame/Loadout.hh>

#include <Client/Ui/Container.hh>
#include <Client/Ui/StaticText.hh>

#include <Client/StaticData.hh>

#include <Client/Game.hh>

#include <format>
#include <map>
#include <cmath>

using namespace Ui;

Element *Ui::UiLoadout::petal_tooltips[PetalID::kNumPetals] = {nullptr};

static void make_petal_tooltip(PetalID::T id) {
    PetalData const &d = PETAL_DATA[id];
    PetalAttributes const &a = d.attributes;
    PoisonDamage const &p = a.poison_damage;
    std::string rld_str = d.reload == 0 ? "" :
        a.secondary_reload == 0 ? std::format("{:g}s ⟳", d.reload) : 
        std::format("{:g}s + {:g}s ⟳", d.reload, a.secondary_reload);
    //std::cout << Renderer::get_ascii_text_size(rld_str.c_str()) << '\n';
    Element *tooltip = new Ui::VContainer({
        new Ui::HFlexContainer(
            new Ui::StaticText(20, d.name, { .fill = 0xffffffff, .h_justify = Style::Left }),
            new Ui::StaticText(16, rld_str, { .fill = 0xffffffff, .v_justify = Style::Top }),
            5, 10, {}
        ),
        new Ui::StaticText(14, RARITY_NAMES[d.rarity], { .fill = RARITY_COLORS[d.rarity], .h_justify = Style::Left }),
        new Ui::Element(0,10),
        new Ui::StaticText(12, d.description, { .fill = 0xffffffff, .h_justify = Style::Left }),
        (d.health || d.damage) ? new Ui::Element(0,10) : nullptr,
        d.health ? new Ui::HContainer({
            new Ui::StaticText(12, "Health: ", { .fill = 0xff66ff66, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{}", std::round(d.health * 10) / 10), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        d.damage ? new Ui::HContainer({
            new Ui::StaticText(12, "Damage: ", { .fill = 0xffff6666, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{}", std::round(d.damage * 10) / 10), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        a.constant_heal ? new Ui::HContainer({
            new Ui::StaticText(12, "Heal: ", { .fill = 0xffff94c9, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{}/s", std::round(a.constant_heal * 10) / 10), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        a.burst_heal ? new Ui::HContainer({
            new Ui::StaticText(12, "Heal: ", { .fill = 0xffff94c9, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{}", std::round(a.burst_heal * 10) / 10), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        p.damage ? new Ui::HContainer({
            new Ui::StaticText(12, "Poison: ", { .fill = 0xffce76db, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{} ({}/s)", std::round(p.damage * p.time * 10) / 10, std::round(p.damage * 10) / 10), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        a.spawns != MobID::kNumMobs ? new Ui::HContainer({
            new Ui::StaticText(12, "Contents: ", { .fill = 0xffd2eb34, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::string((a.spawn_count ? a.spawn_count : 1) * (d.count ? d.count : 1) > 1 ? std::to_string((a.spawn_count ? a.spawn_count : 1) * (d.count ? d.count : 1)) + "x " : "") + MOB_DATA[a.spawns].name + " (", { .fill = 0xffffffff, .h_justify = Style::Left }),
            new Ui::StaticText(12, RARITY_NAMES[MOB_DATA[a.spawns].rarity], { .fill = RARITY_COLORS[MOB_DATA[a.spawns].rarity], .h_justify = Style::Left }),
            new Ui::StaticText(12, ")", { .fill = 0xffffffff, .h_justify = Style::Left }),
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        /* new Ui::Element(0,10),
        new Ui::StaticText(12,
        "Radius: " + std::format("{:g}", d.radius), { .fill =
        0xffffffff, .h_justify = Style::Left }), new Ui::StaticText(12,
        "Count: " + std::to_string(d.count), { .fill =
        0xffffffff, .h_justify = Style::Left }), new Ui::StaticText(12,
        "Clump Radius: " + std::format("{:g}", a.clump_radius), { .fill =
        0xffffffff, .h_justify = Style::Left }), new Ui::StaticText(12,
        "Mass: " + std::format("{:g}", a.mass), { .fill =
        0xffffffff, .h_justify = Style::Left }),new Ui::StaticText(12,
        "Defend Only: " + std::string("True"), { .fill =
        0xffffffff, .h_justify = Style::Left }), new Ui::StaticText(12,
        "Icon Angle: " + std::format("{:g}", a.icon_angle), { .fill =
        0xffffffff, .h_justify = Style::Left }), new Ui::StaticText(12,
        "Rotation Style: " + (std::map<uint8_t, std::string>{ { PetalAttributes::kPassiveRot, "kPassiveRot" }, { PetalAttributes::kNoRot, "kNoRot" }, { PetalAttributes::kFollowRot, "kFollowRot" } })[a.rotation_style], { .fill =
        0xffffffff, .h_justify = Style::Left }) */
    }, 5, 2);
    tooltip->style.fill = 0x80000000;
    tooltip->style.round_radius = 6;
    tooltip->refactor();
    Ui::UiLoadout::petal_tooltips[id] = tooltip;
}

void Ui::make_petal_tooltips() {
    for (PetalID::T i = 0; i < PetalID::kNumPetals; ++i)
        make_petal_tooltip(i);
}