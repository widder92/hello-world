package pdf

import (
	"fmt"
	"strings"
)

func xmlText(s string) string {
	return strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;").Replace(s)
}

// xmp erzeugt die Metadaten des Dokuments. Sie weisen die Datei als
// PDF/A-3b aus und wiederholen Titel, Verfasser und Datum.
func (d *Dokument) xmp() string {
	zeit := d.Zeit.Format("2006-01-02T15:04:05-07:00")
	var b strings.Builder
	b.WriteString("<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n")
	b.WriteString(`<x:xmpmeta xmlns:x="adobe:ns:meta/">` + "\n")
	b.WriteString(` <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">` + "\n")
	b.WriteString(`  <rdf:Description rdf:about="" xmlns:pdfaid="http://www.aiim.org/pdfa/ns/id/">` + "\n")
	b.WriteString("   <pdfaid:part>3</pdfaid:part>\n   <pdfaid:conformance>B</pdfaid:conformance>\n  </rdf:Description>\n")

	b.WriteString(`  <rdf:Description rdf:about="" xmlns:dc="http://purl.org/dc/elements/1.1/">` + "\n")
	b.WriteString("   <dc:format>application/pdf</dc:format>\n")
	if d.Titel != "" {
		fmt.Fprintf(&b, "   <dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">%s</rdf:li></rdf:Alt></dc:title>\n", xmlText(d.Titel))
	}
	if d.Autor != "" {
		fmt.Fprintf(&b, "   <dc:creator><rdf:Seq><rdf:li>%s</rdf:li></rdf:Seq></dc:creator>\n", xmlText(d.Autor))
	}
	if d.Thema != "" {
		fmt.Fprintf(&b, "   <dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">%s</rdf:li></rdf:Alt></dc:description>\n", xmlText(d.Thema))
	}
	b.WriteString("  </rdf:Description>\n")

	b.WriteString(`  <rdf:Description rdf:about="" xmlns:pdf="http://ns.adobe.com/pdf/1.3/">` + "\n")
	if d.Ersteller != "" {
		fmt.Fprintf(&b, "   <pdf:Producer>%s</pdf:Producer>\n", xmlText(d.Ersteller))
	}
	if d.Stichworte != "" {
		fmt.Fprintf(&b, "   <pdf:Keywords>%s</pdf:Keywords>\n", xmlText(d.Stichworte))
	}
	b.WriteString("  </rdf:Description>\n")

	b.WriteString(`  <rdf:Description rdf:about="" xmlns:xmp="http://ns.adobe.com/xap/1.0/">` + "\n")
	if d.Ersteller != "" {
		fmt.Fprintf(&b, "   <xmp:CreatorTool>%s</xmp:CreatorTool>\n", xmlText(d.Ersteller))
	}
	fmt.Fprintf(&b, "   <xmp:CreateDate>%s</xmp:CreateDate>\n   <xmp:ModifyDate>%s</xmp:ModifyDate>\n   <xmp:MetadataDate>%s</xmp:MetadataDate>\n", zeit, zeit, zeit)
	b.WriteString("  </rdf:Description>\n")

	b.WriteString(d.XMPZusatz)
	b.WriteString(" </rdf:RDF>\n</x:xmpmeta>\n")
	// Etwas Leerraum, damit andere Programme die Metadaten an Ort und Stelle
	// ergänzen können.
	for i := 0; i < 10; i++ {
		b.WriteString(strings.Repeat(" ", 100) + "\n")
	}
	b.WriteString(`<?xpacket end="w"?>`)
	return b.String()
}

// ZugferdXMP liefert die Metadaten-Blöcke, an denen ein Empfänger erkennt,
// dass im PDF eine E-Rechnung steckt: Dateiname des Anhangs, Version und
// Profil.
func ZugferdXMP(dateiname, version, profil string) string {
	feld := func(name, beschreibung string) string {
		return "       <rdf:li rdf:parseType=\"Resource\">\n" +
			"        <pdfaProperty:name>" + name + "</pdfaProperty:name>\n" +
			"        <pdfaProperty:valueType>Text</pdfaProperty:valueType>\n" +
			"        <pdfaProperty:category>external</pdfaProperty:category>\n" +
			"        <pdfaProperty:description>" + beschreibung + "</pdfaProperty:description>\n" +
			"       </rdf:li>\n"
	}
	return `  <rdf:Description rdf:about="" xmlns:pdfaExtension="http://www.aiim.org/pdfa/ns/extension/" xmlns:pdfaSchema="http://www.aiim.org/pdfa/ns/schema#" xmlns:pdfaProperty="http://www.aiim.org/pdfa/ns/property#">
   <pdfaExtension:schemas>
    <rdf:Bag>
     <rdf:li rdf:parseType="Resource">
      <pdfaSchema:schema>Factur-X PDFA Extension Schema</pdfaSchema:schema>
      <pdfaSchema:namespaceURI>urn:factur-x:pdfa:CrossIndustryDocument:invoice:1p0#</pdfaSchema:namespaceURI>
      <pdfaSchema:prefix>fx</pdfaSchema:prefix>
      <pdfaSchema:property>
       <rdf:Seq>
` + feld("DocumentFileName", "The name of the embedded XML document") +
		feld("DocumentType", "The type of the hybrid document in capital letters, e.g. INVOICE or ORDER") +
		feld("Version", "The actual version of the standard applying to the embedded XML document") +
		feld("ConformanceLevel", "The conformance level of the embedded XML document") +
		`       </rdf:Seq>
      </pdfaSchema:property>
     </rdf:li>
    </rdf:Bag>
   </pdfaExtension:schemas>
  </rdf:Description>
  <rdf:Description rdf:about="" xmlns:fx="urn:factur-x:pdfa:CrossIndustryDocument:invoice:1p0#">
   <fx:DocumentType>INVOICE</fx:DocumentType>
   <fx:DocumentFileName>` + xmlText(dateiname) + `</fx:DocumentFileName>
   <fx:Version>` + xmlText(version) + `</fx:Version>
   <fx:ConformanceLevel>` + xmlText(profil) + `</fx:ConformanceLevel>
  </rdf:Description>
`
}
