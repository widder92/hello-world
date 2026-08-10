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
