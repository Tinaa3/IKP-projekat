# IKP-projekat
Load Balancing
Razviti servis koji vrši dinamičko balansiranje opterećenja. Servis se
sastoji od dva tipa procesa - Load Balancer (LB) i Worker (WR).  Postoji
jedna instanca LB komponente koja sluša na portu 5059 i prima zahteve za
obradu. Zahtevi za obradu trebaju biti generički.
Servis treba da bude pokrenut na jednoj mašini a WR instance takođe
pokretane na istom računaru. Dok je popunjenost Q između 30% i 70% od
predefinisane veličine broj WR se ne menja. Kada Q sa zahtevima krene da
raste preko 70% - LB treba da pokreće nove procese koji će obavljati dalju
obradu. Kada se Q popuni, potrebno je zaustaviti prihvatanje novih
zahteva. Ukoliko se popunjenost Q spusti ispod 30%, potrebno je postepeno
gasiti WR instance, po jednu na predefinisani interval dok ne ostane samo
jedna instanca.
