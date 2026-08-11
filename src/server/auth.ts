import NextAuth, { type DefaultSession } from "next-auth";
import Credentials from "next-auth/providers/credentials";
import MicrosoftEntraID from "next-auth/providers/microsoft-entra-id";
import { z } from "zod";

import { db } from "@/server/db";
import { verifyPassword } from "@/server/password";

/**
 * Anmeldung.
 *
 * Zwei Wege stehen offen und lassen sich unabhängig voneinander abschalten:
 *
 *  1. Microsoft Entra ID (SSO) – der Regelfall, wenn das Haus Microsoft 365
 *     nutzt. Wird nur aktiviert, wenn die Zugangsdaten hinterlegt sind.
 *  2. Lokale Konten – als Rückfallebene für Personen ohne M365-Konto und für
 *     den Notfall-Zugang, wenn die SSO-Anbindung einmal ausfällt.
 *
 * Die Sitzung liegt als JWT vor, nicht in der Datenbank: so kostet jeder
 * Seitenaufruf keine zusätzliche Abfrage. Die Rollen werden aus dem Token
 * gelesen; sicherheitsrelevante Entscheidungen prüfen zusätzlich in
 * `policy.ts` gegen die Datenbank, damit ein entzogenes Recht nicht bis zum
 * Ablauf des Tokens weitergilt.
 */

const entraConfigured = Boolean(
  process.env.AUTH_MICROSOFT_ENTRA_ID_ID &&
    process.env.AUTH_MICROSOFT_ENTRA_ID_SECRET,
);

export const localLoginEnabled = process.env.LOCAL_LOGIN_ENABLED !== "false";
export const ssoEnabled = entraConfigured;

declare module "next-auth" {
  interface Session {
    user: {
      id: string;
      globalRole: "USER" | "ADMIN";
      siteId: string | null;
    } & DefaultSession["user"];
  }
}

/**
 * Auth.js sagen, unter welcher Adresse die Anwendung erreichbar ist.
 *
 * Ohne diese Angabe baut Auth.js seine Adressen aus der Anfrage – und die
 * trägt im fertigen Bau die *Bindeadresse* des Servers, nicht die Adresse aus
 * der Adresszeile des Browsers. Der Server lauscht auf 0.0.0.0 (also auf allen
 * Netzwerkkarten), und so entstand nach dem Abmelden eine Weiterleitung auf
 * `http://0.0.0.0:3000/anmelden`. Diese Adresse lässt sich nicht aufrufen: Der
 * Browser zeigt ERR_ADDRESS_INVALID, und wer sich abmeldet, landet auf einer
 * Fehlerseite statt bei der Anmeldung.
 *
 * Betroffen waren alle von Auth.js erzeugten Adressen, nicht nur die
 * Abmeldung – beim Anmelden fiel es bloß nicht auf, weil die Oberfläche
 * danach selbst weiterschaltet.
 *
 * `APP_URL` ist ohnehin schon die öffentliche Adresse der Anwendung; sie steht
 * auch unter jedem Link in den Benachrichtigungen. Beide aus derselben Quelle
 * zu speisen hält sie zusammen.
 */
process.env.AUTH_URL ??= process.env.APP_URL;

const credentialsSchema = z.object({
  email: z.string().email(),
  password: z.string().min(1),
});

export const { handlers, signIn, signOut, auth } = NextAuth({
  session: { strategy: "jwt", maxAge: 12 * 60 * 60 },
  pages: {
    signIn: "/anmelden",
    error: "/anmelden",
  },
  trustHost: true,

  providers: [
    ...(entraConfigured
      ? [
          MicrosoftEntraID({
            clientId: process.env.AUTH_MICROSOFT_ENTRA_ID_ID!,
            clientSecret: process.env.AUTH_MICROSOFT_ENTRA_ID_SECRET!,
            issuer: process.env.AUTH_MICROSOFT_ENTRA_ID_ISSUER,
            authorization: { params: { scope: "openid profile email User.Read" } },
          }),
        ]
      : []),

    ...(localLoginEnabled
      ? [
          Credentials({
            name: "Lokales Konto",
            credentials: {
              email: { label: "E-Mail", type: "email" },
              password: { label: "Passwort", type: "password" },
            },
            async authorize(raw) {
              const parsed = credentialsSchema.safeParse(raw);
              if (!parsed.success) return null;

              const { email, password } = parsed.data;
              const user = await db.user.findUnique({
                where: { email: email.toLowerCase().trim() },
                select: {
                  id: true,
                  email: true,
                  name: true,
                  image: true,
                  passwordHash: true,
                  isActive: true,
                  isExternal: true,
                  globalRole: true,
                  siteId: true,
                },
              });

              // Kein Konto, kein Passwort hinterlegt (reines SSO-Konto),
              // gesperrt oder ein extern angelegter Melder: alle Fälle
              // scheitern gleich, damit sich aus der Antwort nicht ablesen
              // lässt, welche Adressen existieren.
              if (
                !user ||
                !user.passwordHash ||
                !user.isActive ||
                user.isExternal
              ) {
                return null;
              }

              const ok = await verifyPassword(user.passwordHash, password);
              if (!ok) return null;

              await db.user.update({
                where: { id: user.id },
                data: { lastLoginAt: new Date() },
              });

              return {
                id: user.id,
                email: user.email,
                name: user.name,
                image: user.image,
              };
            },
          }),
        ]
      : []),
  ],

  callbacks: {
    /**
     * Bei SSO wird das Konto anhand der E-Mail-Adresse zugeordnet. Es entsteht
     * bewusst kein neuer Benutzer: Wer im Ticketsystem arbeiten soll, muss von
     * der Verwaltung angelegt oder importiert worden sein. Sonst könnte jede
     * Person im Mandanten sich selbst ein Konto verschaffen.
     */
    async signIn({ user, account }) {
      if (account?.provider !== "microsoft-entra-id") return true;
      if (!user.email) return false;

      const existing = await db.user.findUnique({
        where: { email: user.email.toLowerCase() },
        select: { id: true, isActive: true, entraOid: true },
      });

      if (!existing || !existing.isActive) return false;

      // Objekt-ID beim ersten SSO-Login festhalten, damit spätere
      // Namensänderungen die Zuordnung nicht zerreißen.
      const oid =
        typeof account.providerAccountId === "string"
          ? account.providerAccountId
          : null;
      if (oid && existing.entraOid !== oid) {
        await db.user.update({
          where: { id: existing.id },
          data: { entraOid: oid, lastLoginAt: new Date() },
        });
      } else {
        await db.user.update({
          where: { id: existing.id },
          data: { lastLoginAt: new Date() },
        });
      }

      return true;
    },

    async jwt({ token, user, trigger }) {
      // Beim Anmelden und bei einem ausdrücklichen Aktualisieren die
      // Stammdaten frisch aus der Datenbank holen.
      if (user?.email || trigger === "update") {
        const email = user?.email ?? token.email;
        if (email) {
          const record = await db.user.findUnique({
            where: { email: email.toLowerCase() },
            select: {
              id: true,
              name: true,
              image: true,
              globalRole: true,
              siteId: true,
              isActive: true,
            },
          });
          if (record && record.isActive) {
            token.sub = record.id;
            token.name = record.name;
            token.picture = record.image;
            token.globalRole = record.globalRole;
            token.siteId = record.siteId;
          }
        }
      }
      return token;
    },

    async session({ session, token }) {
      if (token.sub) session.user.id = token.sub;
      session.user.globalRole =
        (token.globalRole as "USER" | "ADMIN" | undefined) ?? "USER";
      session.user.siteId = (token.siteId as string | null | undefined) ?? null;
      return session;
    },
  },
});
