import { Fragment, type ReactNode } from "react";

/**
 * Schlanke Markdown-Darstellung für Wissensartikel.
 *
 * Baut React-Elemente statt HTML-Zeichenketten. Es gibt also kein
 * `dangerouslySetInnerHTML` und damit auch keine Möglichkeit, über einen
 * Artikeltext Skriptcode in die Oberfläche zu bringen – auch dann nicht, wenn
 * ein Text aus einem Import oder aus einer Mail stammt.
 *
 * Unterstützt bewusst nur, was in Anleitungen tatsächlich vorkommt:
 * Überschriften, Absätze, Aufzählungen, nummerierte Listen, Zitate, Fettdruck,
 * Kursiv, Code und Verweise.
 */
export function Markdown({ text }: { text: string }) {
  return <div className="space-y-4">{renderBloecke(text)}</div>;
}

function renderBloecke(text: string): ReactNode[] {
  const zeilen = text.replace(/\r\n/g, "\n").split("\n");
  const bloecke: ReactNode[] = [];

  let i = 0;
  let key = 0;

  while (i < zeilen.length) {
    const zeile = zeilen[i];

    // Leerzeile
    if (zeile.trim() === "") {
      i++;
      continue;
    }

    // Codeblock
    if (zeile.trim().startsWith("```")) {
      const inhalt: string[] = [];
      i++;
      while (i < zeilen.length && !zeilen[i].trim().startsWith("```")) {
        inhalt.push(zeilen[i]);
        i++;
      }
      i++; // schließendes ```
      bloecke.push(
        <pre
          key={key++}
          className="overflow-x-auto rounded-lg border border-line bg-surface-2 p-3.5 text-sm"
        >
          <code className="font-mono">{inhalt.join("\n")}</code>
        </pre>,
      );
      continue;
    }

    // Überschrift
    const ueberschrift = zeile.match(/^(#{1,4})\s+(.*)$/);
    if (ueberschrift) {
      const stufe = ueberschrift[1].length;
      const inhalt = renderInline(ueberschrift[2]);
      const klassen = [
        "text-2xl font-semibold tracking-tight text-fg mt-8 first:mt-0",
        "text-xl font-semibold tracking-tight text-fg mt-7 first:mt-0",
        "text-lg font-semibold text-fg mt-6 first:mt-0",
        "text-base font-semibold text-fg mt-5 first:mt-0",
      ][stufe - 1];

      const Tag = (["h2", "h3", "h4", "h5"] as const)[stufe - 1];
      bloecke.push(
        <Tag key={key++} className={klassen}>
          {inhalt}
        </Tag>,
      );
      i++;
      continue;
    }

    // Zitat
    if (zeile.trimStart().startsWith(">")) {
      const inhalt: string[] = [];
      while (i < zeilen.length && zeilen[i].trimStart().startsWith(">")) {
        inhalt.push(zeilen[i].replace(/^\s*>\s?/, ""));
        i++;
      }
      bloecke.push(
        <blockquote
          key={key++}
          className="border-l-4 border-primary/40 bg-surface-2 py-2 pl-4 pr-3 text-fg-muted"
        >
          {renderInline(inhalt.join(" "))}
        </blockquote>,
      );
      continue;
    }

    // Nummerierte Liste
    if (/^\s*\d+\.\s+/.test(zeile)) {
      const punkte: string[] = [];
      while (i < zeilen.length && /^\s*\d+\.\s+/.test(zeilen[i])) {
        punkte.push(zeilen[i].replace(/^\s*\d+\.\s+/, ""));
        i++;
      }
      bloecke.push(
        <ol key={key++} className="ml-5 list-decimal space-y-1.5 text-fg">
          {punkte.map((p, n) => (
            <li key={n} className="pl-1 leading-relaxed">
              {renderInline(p)}
            </li>
          ))}
        </ol>,
      );
      continue;
    }

    // Aufzählung
    if (/^\s*[-*•]\s+/.test(zeile)) {
      const punkte: string[] = [];
      while (i < zeilen.length && /^\s*[-*•]\s+/.test(zeilen[i])) {
        punkte.push(zeilen[i].replace(/^\s*[-*•]\s+/, ""));
        i++;
      }
      bloecke.push(
        <ul key={key++} className="ml-5 list-disc space-y-1.5 text-fg">
          {punkte.map((p, n) => (
            <li key={n} className="pl-1 leading-relaxed">
              {renderInline(p)}
            </li>
          ))}
        </ul>,
      );
      continue;
    }

    // Trennlinie
    if (/^\s*(-{3,}|\*{3,})\s*$/.test(zeile)) {
      bloecke.push(<hr key={key++} className="border-line" />);
      i++;
      continue;
    }

    // Absatz: zusammenhängende Zeilen bis zur nächsten Leerzeile
    const absatz: string[] = [];
    while (
      i < zeilen.length &&
      zeilen[i].trim() !== "" &&
      !/^(#{1,4}\s|\s*[-*•]\s|\s*\d+\.\s|\s*>|```)/.test(zeilen[i])
    ) {
      absatz.push(zeilen[i]);
      i++;
    }
    bloecke.push(
      <p key={key++} className="leading-relaxed text-fg">
        {renderInline(absatz.join(" "))}
      </p>,
    );
  }

  return bloecke;
}

/**
 * Auszeichnungen innerhalb einer Zeile.
 *
 * Arbeitet zeichenweise über eine Musterliste, damit verschachtelte und
 * unvollständige Auszeichnungen nicht zu falschen Ergebnissen führen.
 */
function renderInline(text: string): ReactNode {
  const teile: ReactNode[] = [];
  let rest = text;
  let key = 0;

  const muster: {
    regex: RegExp;
    render: (m: RegExpMatchArray, k: number) => ReactNode;
  }[] = [
    {
      // [Text](Ziel)
      regex: /\[([^\]]+)\]\(([^)\s]+)\)/,
      render: (m, k) => {
        const ziel = m[2];
        // Nur harmlose Protokolle zulassen – "javascript:" wäre sonst ein
        // Einfallstor.
        const sicher =
          /^(https?:\/\/|mailto:|tel:|\/)/i.test(ziel) ? ziel : "#";
        const extern = /^https?:\/\//i.test(sicher);
        return (
          <a
            key={k}
            href={sicher}
            {...(extern
              ? { target: "_blank", rel: "noopener noreferrer" }
              : {})}
            className="font-medium text-primary underline-offset-2 hover:underline"
          >
            {m[1]}
          </a>
        );
      },
    },
    {
      regex: /\*\*([^*]+)\*\*/,
      render: (m, k) => (
        <strong key={k} className="font-semibold">
          {m[1]}
        </strong>
      ),
    },
    {
      regex: /(?<!\*)\*([^*]+)\*(?!\*)/,
      render: (m, k) => (
        <em key={k} className="italic">
          {m[1]}
        </em>
      ),
    },
    {
      regex: /`([^`]+)`/,
      render: (m, k) => (
        <code
          key={k}
          className="rounded bg-surface-2 px-1 py-0.5 font-mono text-[0.875em]"
        >
          {m[1]}
        </code>
      ),
    },
  ];

  while (rest.length > 0) {
    let fruehester: { index: number; treffer: RegExpMatchArray; idx: number } | null =
      null;

    for (const [idx, m] of muster.entries()) {
      const treffer = rest.match(m.regex);
      if (treffer?.index !== undefined) {
        if (!fruehester || treffer.index < fruehester.index) {
          fruehester = { index: treffer.index, treffer, idx };
        }
      }
    }

    if (!fruehester) {
      teile.push(<Fragment key={key++}>{rest}</Fragment>);
      break;
    }

    if (fruehester.index > 0) {
      teile.push(
        <Fragment key={key++}>{rest.slice(0, fruehester.index)}</Fragment>,
      );
    }

    teile.push(muster[fruehester.idx].render(fruehester.treffer, key++));
    rest = rest.slice(fruehester.index + fruehester.treffer[0].length);
  }

  return teile;
}
