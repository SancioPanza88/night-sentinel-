NIGHT SENTINEL - demo tecnica PS1 (PSn00bSDK 0.24)
Avvio: apri nightsentinel.cue in DuckStation (BIOS PS1 richiesto da DuckStation).

COMANDI
  D-pad / stick sinistro   muovi (relativo alla camera)
  X                        salta  (in aria: tieni X = planata alla Batman,
                           mantello ad ala, si inclina curvando)
  Quadrato                 combo da 3 colpi: premi Quadrato ancora a ritmo
                           (jab destro, gancio sinistro, calcio rotante a 360)
  Triangolo                calcio frontale potente
  R1                       impulso energetico (costa 25 energia)
  Cerchio                  rotolata a terra in avanti, stile Arkham (annulla anche i colpi)
  R2                       rampino (vicino ai punti dorati sui tetti)
  L1                       aggancia il nemico piu' vicino (di nuovo L1 = sgancia;
                           senza nemici ricentra la camera)
  L2 / stick destro        con l'aggancio attivo: cambia bersaglio
  Stick destro o SELECT+Sinistra/Destra   ruota la camera (senza aggancio)
  START                    inizia / rigioca

MISSIONE "PRIMO TURNO"
  1. Sali sul tetto del magazzino (edificio marrone a destra) col rampino.
  2. Sconfiggi i due nemici che compaiono in strada:
     il Bruto (lento, martellata e gancio, non si ferma per i colpi leggeri)
     e lo Scattante (verde: ti gira intorno e poi balza con un calcio volante).
  3. Recupera il nucleo dati nell'edificio blu (sinistra, in fondo).
  Nell'edificio verde c'e' una console che ricarica vita ed energia.

SORGENTI: main.c, city_data.h (tools/gen_city.py), model_data_v3.h (il tuo personaggio v3).
Ricompilare: cmake --preset default && cmake --build ./build  (con PSn00bSDK nel PATH)
