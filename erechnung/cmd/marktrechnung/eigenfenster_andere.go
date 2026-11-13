//go:build !(windows && amd64)

package main

// Das eigene Programmfenster gibt es unter Windows; auf den anderen
// Systemen zeigt Marktrechnung die Oberfläche im App-Fenster des Browsers.

const endeKeinWebView = 3

func eigenesFensterVerfuegbar() bool { return false }

func holeFensterNachVorn() bool { return false }

func zeigeEigenesFenster(adresse, profil string) int { return endeKeinWebView }
