package dec

import "testing"

func TestParseUndText(t *testing.T) {
	faelle := []struct{ ein, aus string }{
		{"0", "0"}, {"12", "12"}, {"12.5", "12.5"}, {"12,50", "12.5"},
		{"1.234,56", "1234.56"}, {"-0.005", "-0.005"}, {"+3", "3"},
		{"0.1234567", "0.123457"}, {".5", "0.5"}, {"7.", "7"},
	}
	for _, f := range faelle {
		d, err := Parse(f.ein)
		if err != nil {
			t.Fatalf("Parse(%q): %v", f.ein, err)
		}
		if d.String() != f.aus {
			t.Errorf("Parse(%q) = %s, erwartet %s", f.ein, d, f.aus)
		}
	}
	for _, s := range []string{"", "abc", "1.2.3", "-", "1e5", "12 34"} {
		if _, err := Parse(s); err == nil {
			t.Errorf("Parse(%q) hätte scheitern müssen", s)
		}
	}
}

func TestRunden(t *testing.T) {
	faelle := []struct {
		ein string
		n   int
		aus string
	}{
		{"1.005", 2, "1.01"}, {"1.004999", 2, "1.00"}, {"-1.005", 2, "-1.01"},
		{"2.5", 0, "3"}, {"-2.5", 0, "-3"}, {"0.125", 2, "0.13"}, {"1.115", 2, "1.12"},
	}
	for _, f := range faelle {
		if got := Muss(f.ein).Runde(f.n).Fest(f.n); got != f.aus {
			t.Errorf("Runde(%s,%d) = %s, erwartet %s", f.ein, f.n, got, f.aus)
		}
	}
}

func TestRechnen(t *testing.T) {
	// 3 x 1,115 = 3,345 -> 3,35 (mit Gleitkomma käme 3,34 heraus)
	if got := Muss("3").Mal(Muss("1.115"), 2).Fest(2); got != "3.35" {
		t.Errorf("3 x 1.115 = %s", got)
	}
	if got := Muss("0.1").Mal(Muss("0.2"), 6).String(); got != "0.02" {
		t.Errorf("0.1 x 0.2 = %s", got)
	}
	if got := Muss("-2.5").Mal(Muss("1.01"), 2).Fest(2); got != "-2.53" {
		t.Errorf("-2.5 x 1.01 = %s", got)
	}
	if got := Muss("100").Prozent(Muss("19"), 2).Fest(2); got != "19.00" {
		t.Errorf("19 %% von 100 = %s", got)
	}
	if got := Muss("33.33").Prozent(Muss("7"), 2).Fest(2); got != "2.33" {
		t.Errorf("7 %% von 33.33 = %s", got)
	}
	if got := Muss("10").Durch(Muss("3"), 4).String(); got != "3.3333" {
		t.Errorf("10 / 3 = %s", got)
	}
	if got := Muss("999999.99").Mal(Muss("999999.9999"), 2).Fest(2); got != "999999989900.00" {
		t.Errorf("große Zahlen: %s", got)
	}
}

func TestDeutsch(t *testing.T) {
	if got := Muss("1234567.5").Deutsch(2, 2); got != "1.234.567,50" {
		t.Errorf("Deutsch = %s", got)
	}
	if got := Muss("-12.345").Deutsch(0, 3); got != "-12,345" {
		t.Errorf("Deutsch = %s", got)
	}
	if got := Muss("100").Deutsch(0, 2); got != "100" {
		t.Errorf("Deutsch = %s", got)
	}
}
