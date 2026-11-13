package geheim

import "testing"

func TestAblage(t *testing.T) {
	a := Neu(t.TempDir(), "post.geheim")
	if a.Vorhanden() {
		t.Error("leere Ablage meldet einen Inhalt")
	}
	if s, err := a.Lies(); s != "" || err != nil {
		t.Errorf("leer: %q %v", s, err)
	}
	if err := a.Setze("Geheim! äöü 123"); err != nil {
		t.Fatal(err)
	}
	if s, err := a.Lies(); s != "Geheim! äöü 123" || err != nil || !a.Vorhanden() {
		t.Errorf("gelesen: %q %v", s, err)
	}
	if err := a.Setze(""); err != nil || a.Vorhanden() {
		t.Errorf("Löschen: %v", err)
	}
}
