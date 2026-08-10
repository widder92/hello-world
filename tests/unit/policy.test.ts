import { TeamMode, TeamRole } from "@prisma/client";
import { describe, expect, it } from "vitest";

import { capabilitiesFor } from "@/server/capabilities";
import {
  agentTicketFilter,
  canAccessQueue,
  canAssignToOthers,
  canManageTeam,
  canViewStatistics,
  canWorkTicket,
  inventoryAccess,
  inventoryTeamIds,
  requireInventory,
  requireTicketWork,
  ticketAccessLevel,
  ticketVisibilityFilter,
  type Actor,
  type TicketAccessSubject,
} from "@/server/policy";

/**
 * Die Rechteprüfung ist die Stelle, an der ein Fehler nicht auffällt, bis er
 * schadet: Sichtbare Vorgänge fallen sofort auf, unsichtbar zu viel Sichtbares
 * nicht.
 *
 * Die entscheidende Regel steht in `ticketAccessLevel`: Bearbeiten verlangt
 * **beides**, Mitgliedschaft im Bereich UND Freigabe für den Eingang. Ohne die
 * zweite Bedingung käme ein Hausmeister aus Musterstadt an die Vorgänge der
 * Schule, obwohl beide im selben Bereich arbeiten – und die standortgenauen
 * Eingänge wären nur eine Sortierhilfe statt einer Trennung.
 */

const EINGANG_NORD = "queue-hm-nord";
const EINGANG_SCHULE = "queue-hm-schule";
const EINGANG_IT = "queue-it";

const TEAM_HM = "team-hausmeister";
const TEAM_IT = "team-it";

function person(teil: Partial<Actor> = {}): Actor {
  return {
    id: "user-1",
    name: "Frank Berger",
    email: "frank.berger@example.org",
    isAdmin: false,
    siteId: "site-nord",
    a11yPrefs: {},
    theme: "system",
    memberships: [],
    queues: [],
    defaultQueueId: null,
    ...teil,
  };
}

function mitgliedschaft(
  teamId: string,
  role: TeamRole = TeamRole.MEMBER,
  mode: TeamMode = TeamMode.VOLL,
  teamHasInventory = false,
) {
  return {
    teamId,
    teamKey: teamId,
    teamName: teamId,
    teamMode: mode,
    role,
    capabilities: capabilitiesFor(mode),
    teamHasInventory,
  };
}

function eingang(id: string, teamId: string, siteId: string | null = null) {
  return {
    id,
    name: id,
    teamId,
    siteId,
    siteName: siteId,
    isDefault: false,
  };
}

function vorgang(teil: Partial<TicketAccessSubject> = {}): TicketAccessSubject {
  return {
    id: "ticket-1",
    queueId: EINGANG_NORD,
    teamId: TEAM_HM,
    requesterId: "melderin",
    watchers: [],
    ...teil,
  };
}

/** Hausmeister, Mitglied im Bereich, freigegeben nur für Wohnheim Nord. */
const hausmeisterNord = person({
  memberships: [mitgliedschaft(TEAM_HM)],
  queues: [eingang(EINGANG_NORD, TEAM_HM, "site-nord")],
});

describe("Zugriff auf einen einzelnen Vorgang", () => {
  it("gibt Bearbeitungszugriff bei Mitgliedschaft UND freigegebenem Eingang", () => {
    expect(ticketAccessLevel(hausmeisterNord, vorgang())).toBe("agent");
  });

  it("verweigert den Zugriff auf einen nicht freigegebenen Eingang desselben Bereichs", () => {
    // Der Kern der standortgenauen Trennung: gleicher Bereich, anderes Haus.
    const schulticket = vorgang({ queueId: EINGANG_SCHULE });

    expect(ticketAccessLevel(hausmeisterNord, schulticket)).toBe("none");
    expect(canWorkTicket(hausmeisterNord, schulticket)).toBe(false);
    expect(() => requireTicketWork(hausmeisterNord, schulticket)).toThrow();
  });

  it("verweigert den Zugriff auf einen fremden Bereich", () => {
    const itTicket = vorgang({ queueId: EINGANG_IT, teamId: TEAM_IT });
    expect(ticketAccessLevel(hausmeisterNord, itTicket)).toBe("none");
  });

  it("verweigert den Zugriff, wenn der Eingang zwar freigegeben, der Bereich aber fremd ist", () => {
    // Kann durch eine stehengebliebene Freigabe nach einem Bereichswechsel
    // entstehen. Eine Freigabe allein reicht nicht.
    const nurFreigabe = person({
      memberships: [],
      queues: [eingang(EINGANG_NORD, TEAM_HM, "site-nord")],
    });

    expect(ticketAccessLevel(nurFreigabe, vorgang())).toBe("none");
  });

  it("lässt den Melder den eigenen Vorgang lesen, aber nicht bearbeiten", () => {
    const melderin = person({ id: "melderin" });
    const eigener = vorgang({ requesterId: "melderin" });

    expect(ticketAccessLevel(melderin, eigener)).toBe("requester");
    expect(canWorkTicket(melderin, eigener)).toBe(false);
  });

  it("lässt Beobachter mitlesen", () => {
    const kollege = person({ id: "kollege" });
    const beobachtet = vorgang({ watchers: [{ userId: "kollege" }] });

    expect(ticketAccessLevel(kollege, beobachtet)).toBe("requester");
  });

  it("verweigert den Zugriff auf einen fremden Vorgang vollständig", () => {
    // Der Fall aus der Abnahme: fremdes Ticket über die Adresszeile aufrufen.
    expect(ticketAccessLevel(person({ id: "fremde" }), vorgang())).toBe("none");
  });

  it("gibt der Administration Zugriff auf alles", () => {
    const admin = person({ isAdmin: true });
    expect(ticketAccessLevel(admin, vorgang({ queueId: EINGANG_SCHULE }))).toBe(
      "agent",
    );
  });
});

describe("Freigabe von Eingängen", () => {
  it("erlaubt nur freigegebene Eingänge", () => {
    expect(canAccessQueue(hausmeisterNord, EINGANG_NORD)).toBe(true);
    expect(canAccessQueue(hausmeisterNord, EINGANG_SCHULE)).toBe(false);
  });

  it("erlaubt der Administration jeden Eingang", () => {
    expect(canAccessQueue(person({ isAdmin: true }), EINGANG_SCHULE)).toBe(true);
  });
});

describe("Filter für Ticketlisten", () => {
  it("schließt eigene, beobachtete und dienstliche Vorgänge ein", () => {
    const filter = ticketVisibilityFilter(hausmeisterNord) as {
      OR: Record<string, unknown>[];
    };

    expect(filter.OR).toContainEqual({ requesterId: "user-1" });
    expect(filter.OR).toContainEqual({
      watchers: { some: { userId: "user-1" } },
    });
    // Der dienstliche Teil verlangt beides gleichzeitig.
    expect(filter.OR).toContainEqual({
      AND: [
        { queueId: { in: [EINGANG_NORD] } },
        { teamId: { in: [TEAM_HM] } },
      ],
    });
  });

  it("lässt einer Person ohne Bereich nur die eigenen Vorgänge", () => {
    const melderin = person({ memberships: [], queues: [] });
    const filter = ticketVisibilityFilter(melderin) as {
      OR: Record<string, unknown>[];
    };

    expect(filter.OR).toHaveLength(2);
    expect(JSON.stringify(filter)).not.toContain("queueId");
  });

  it("gibt der Administration einen offenen Filter", () => {
    expect(ticketVisibilityFilter(person({ isAdmin: true }))).toEqual({});
  });
});

describe("Filter für die Arbeitsansichten", () => {
  it("beschränkt auf die freigegebenen Eingänge", () => {
    expect(agentTicketFilter(hausmeisterNord)).toEqual({
      queueId: { in: [EINGANG_NORD] },
    });
  });

  it("wirft einen angefragten, nicht freigegebenen Eingang heraus", () => {
    // Die Eingänge kommen als Filter aus der Adresszeile und sind damit frei
    // wählbar. Ohne diesen Abgleich könnte man sich einen fremden Eingang
    // einfach in die URL schreiben.
    expect(
      agentTicketFilter(hausmeisterNord, [EINGANG_NORD, EINGANG_SCHULE]),
    ).toEqual({ queueId: { in: [EINGANG_NORD] } });
  });

  it("liefert ohne jede Freigabe eine Bedingung, die nichts trifft", () => {
    // Der gefährlichste Fall: Ein leerer Filter gäbe *alle* Vorgänge zurück.
    expect(agentTicketFilter(person())).toEqual({ id: { in: [] } });
  });

  it("liefert auch bei ausschließlich fremden Eingängen nichts", () => {
    expect(agentTicketFilter(hausmeisterNord, [EINGANG_SCHULE])).toEqual({
      id: { in: [] },
    });
  });
});

describe("Rollen innerhalb eines Bereichs", () => {
  it("lässt nur Leitung und Administration den Bereich verwalten", () => {
    const mitglied = person({ memberships: [mitgliedschaft(TEAM_HM)] });
    const leitung = person({
      memberships: [mitgliedschaft(TEAM_HM, TeamRole.LEAD)],
    });

    expect(canManageTeam(mitglied, TEAM_HM)).toBe(false);
    expect(canManageTeam(leitung, TEAM_HM)).toBe(true);
    expect(canManageTeam(person({ isAdmin: true }), TEAM_HM)).toBe(true);
  });

  it("lässt nur Verteilung und Leitung anderen zuweisen", () => {
    expect(
      canAssignToOthers(
        person({ memberships: [mitgliedschaft(TEAM_HM)] }),
        TEAM_HM,
      ),
    ).toBe(false);
    expect(
      canAssignToOthers(
        person({ memberships: [mitgliedschaft(TEAM_HM, TeamRole.DISPATCHER)] }),
        TEAM_HM,
      ),
    ).toBe(true);
  });

  it("erlaubt keine Zuweisung in einem Bereich der schlanken Stufe", () => {
    // Dort gibt es die Funktion nicht – auch nicht für die Leitung.
    const leitungEinfach = person({
      memberships: [mitgliedschaft(TEAM_HM, TeamRole.LEAD, TeamMode.EINFACH)],
    });

    expect(canAssignToOthers(leitungEinfach, TEAM_HM)).toBe(false);
    expect(canViewStatistics(leitungEinfach, TEAM_HM)).toBe(false);
  });

  it("zeigt die Auswertung der Leitung eines vollen Bereichs", () => {
    const leitung = person({
      memberships: [mitgliedschaft(TEAM_HM, TeamRole.LEAD)],
    });

    expect(canViewStatistics(leitung, TEAM_HM)).toBe(true);
    expect(canViewStatistics(leitung, TEAM_IT)).toBe(false);
  });
});

/**
 * Zugang zum Inventar.
 *
 * Er hängt nicht an der Administration, sondern am Bereich: Wer in einem
 * Bereich mit Inventar arbeitet, kommt hinein. Die Stammdatenlisten wirken
 * dagegen auf alle Geräte gleichzeitig und bleiben bei der Leitung.
 */
describe("Inventar", () => {
  const TEAM_OHNE = "team-kueche";

  it("lässt die Administration alles", () => {
    const admin = person({ isAdmin: true, memberships: [] });
    expect(inventoryAccess(admin)).toBe("manage");
  });

  it("gibt der Leitung eines Inventarbereichs die Stammdaten", () => {
    const leitung = person({
      memberships: [mitgliedschaft(TEAM_IT, TeamRole.LEAD, TeamMode.VOLL, true)],
    });
    expect(inventoryAccess(leitung)).toBe("manage");
    expect(() => requireInventory(leitung, "manage")).not.toThrow();
  });

  it("lässt Mitglieder Geräte pflegen, aber keine Stammdaten", () => {
    const mitglied = person({
      memberships: [
        mitgliedschaft(TEAM_IT, TeamRole.MEMBER, TeamMode.VOLL, true),
      ],
    });

    expect(inventoryAccess(mitglied)).toBe("read");
    // Geräte ja …
    expect(() => requireInventory(mitglied)).not.toThrow();
    // … Gerätearten und Kostenstellen nein.
    expect(() => requireInventory(mitglied, "manage")).toThrow();
  });

  it("sperrt Bereiche ohne Inventar aus", () => {
    // Die Küche arbeitet im System, führt aber kein Inventar. Auch ihre
    // Leitung hat dort nichts zu suchen.
    const kuechenLeitung = person({
      memberships: [
        mitgliedschaft(TEAM_OHNE, TeamRole.LEAD, TeamMode.EINFACH, false),
      ],
    });

    expect(inventoryAccess(kuechenLeitung)).toBe("none");
    expect(() => requireInventory(kuechenLeitung)).toThrow();
  });

  it("sperrt Personen ohne Bereich aus", () => {
    const melderin = person({ memberships: [], queues: [] });
    expect(inventoryAccess(melderin)).toBe("none");
  });

  it("nennt nur die Bereiche, die tatsächlich ein Inventar führen", () => {
    // Wer in zwei Bereichen arbeitet, darf trotzdem nur Geräte des einen
    // sehen – sonst wanderten Geräte zwischen den Bereichen.
    const beides = person({
      memberships: [
        mitgliedschaft(TEAM_IT, TeamRole.MEMBER, TeamMode.VOLL, true),
        mitgliedschaft(TEAM_OHNE, TeamRole.LEAD, TeamMode.EINFACH, false),
      ],
    });

    expect(inventoryTeamIds(beides)).toEqual([TEAM_IT]);
  });

  it("macht aus einer Leitung im falschen Bereich keine Inventarleitung", () => {
    // Leitung der Küche, einfaches Mitglied in der IT: Die Stammdaten der IT
    // gehen sie nichts an.
    const gemischt = person({
      memberships: [
        mitgliedschaft(TEAM_OHNE, TeamRole.LEAD, TeamMode.EINFACH, false),
        mitgliedschaft(TEAM_IT, TeamRole.MEMBER, TeamMode.VOLL, true),
      ],
    });

    expect(inventoryAccess(gemischt)).toBe("read");
  });
});
