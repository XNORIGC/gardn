#include <Client/Ui/InGame/Loadout.hh>

#include <Client/Ui/Container.hh>
#include <Client/Ui/StaticText.hh>

#include <Client/StaticData.hh>
#include <Client/Game.hh>

#include <format>
#include <map>

using namespace Ui;

Element *Ui::UiLoadout::petal_tooltips[PetalID::kNumPetals] = {nullptr};

static void make_petal_tooltip(PetalID::T id) {
    PetalData const &d = PETAL_DATA[id];
    PetalAttributes const &a = d.attributes;
    std::string rld_str = d.reload == 0 ? "" :
        a.secondary_reload == 0 ? std::format("{:.1f}s ⟳", d.reload) : 
        std::format("{:.1f}s + {:.1f}s ⟳", d.reload, a.secondary_reload);
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
        (d.damage || d.health) ? new Ui::Element(0,10) : nullptr,
        d.damage ? new Ui::HContainer({
            new Ui::StaticText(12, "Damage: ", { .fill = 0xffff6666, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{:g}", d.damage), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        d.health ? new Ui::HContainer({
            new Ui::StaticText(12, "Health: ", { .fill = 0xff66ff66, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{:g}", d.health), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        a.constant_heal ? new Ui::HContainer({
            new Ui::StaticText(12, "Heal: ", { .fill = 0xffff94c9, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{:g}/s", a.constant_heal), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        a.burst_heal ? new Ui::HContainer({
            new Ui::StaticText(12, "Heal: ", { .fill = 0xffff94c9, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{:g}", a.burst_heal), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        a.poison_damage.damage ? new Ui::HContainer({
            new Ui::StaticText(12, "Poison: ", { .fill = 0xffce76db, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{:g} ({:.2g}/s)", a.poison_damage.damage * a.poison_damage.time, a.poison_damage.damage), { .fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        a.spawns != MobID::kNumMobs ? new Ui::HContainer({
            new Ui::StaticText(12, "Contents: ", { .fill = 0xffd2eb34, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::string((a.spawn_count ? a.spawn_count : 1) * (d.count ? d.count : 1) > 1 ? std::to_string((a.spawn_count ? a.spawn_count : 1) * (d.count ? d.count : 1)) + "x " : "") + MOB_DATA[a.spawns].name + " (", { .fill = 0xffffffff, .h_justify = Style::Left }),
            new Ui::StaticText(12, RARITY_NAMES[MOB_DATA[a.spawns].rarity], { .fill = RARITY_COLORS[MOB_DATA[a.spawns].rarity], .h_justify = Style::Left }),
            new Ui::StaticText(12, ")", { .fill = 0xffffffff, .h_justify = Style::Left }),
        }, 0, 0, { .h_justify = Style::Left }) : nullptr,
        a.extra_health ? new Ui::HContainer({
            new Ui::StaticText(12, "Flower Health: ", {.fill = 0xff66ff66, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("+{:g}", a.extra_health), {.fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, {.h_justify = Style::Left }) : nullptr,
        a.movement_speed ? new Ui::HContainer({
            new Ui::StaticText(12, "Movement Speed: ", {.fill = 0xffcde23b, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("+{:g}%",  a.movement_speed * 100), {.fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, {.h_justify = Style::Left }) : nullptr,
        a.reduce_reload ? new Ui::HContainer({
            new Ui::StaticText(12, "Reload: ", {.fill = 0xffcde23b, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{:g}%", (a.reduce_reload - 1) * 100), {.fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, {.h_justify = Style::Left }) : nullptr,
        a.extra_range ? new Ui::HContainer({
            new Ui::StaticText(12, "Attack Range: ", {.fill = 0xffcde23b, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("+{:g}", a.extra_range ), {.fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, {.h_justify = Style::Left }) : nullptr,
        a.extra_vision ? new Ui::HContainer({
            new Ui::StaticText(12, "Extra Vision: ", {.fill = 0xffcde23b, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{:g}%", 1 / (1 - a.extra_vision) * 100), {.fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, {.h_justify = Style::Left }) : nullptr,
        a.armor ? new Ui::HContainer({
            new Ui::StaticText(12, "Armor: ", {.fill = 0xffcde23b, .h_justify = Style::Left }),
            new Ui::StaticText(12, std::format("{:g}", a.armor), {.fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, {.h_justify = Style::Left }) : nullptr,
        a.controls ? new Ui::HContainer({
            new Ui::StaticText(12, "Controls: ", {.fill = 0xffcde23b, .h_justify = Style::Left }),
            new Ui::StaticText(12, PETAL_DATA[a.controls].name, {.fill = 0xffffffff, .h_justify = Style::Left })
        }, 0, 0, {.h_justify = Style::Left }) : nullptr,
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