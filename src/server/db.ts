import { PrismaClient } from "@prisma/client";

/**
 * Ein einziger Prisma-Client pro Prozess. Im Entwicklungsmodus lädt Next.js
 * Module bei jeder Änderung neu; ohne diesen Zwischenspeicher entstünde bei
 * jedem Neuladen eine neue Verbindung, bis die Datenbank keine mehr annimmt.
 */
const globalForPrisma = globalThis as unknown as {
  prisma: PrismaClient | undefined;
};

export const db =
  globalForPrisma.prisma ??
  new PrismaClient({
    log:
      process.env.NODE_ENV === "development"
        ? ["warn", "error"]
        : ["error"],
  });

if (process.env.NODE_ENV !== "production") {
  globalForPrisma.prisma = db;
}
