REM Adds rows to data/: the ID is assigned automatically.
REM Quote a value that contains a comma: 'Rossi, Jr'. Apostrophes need nothing: D'Annunzio, L'Aquila
INSERT INTO EMPLOY VALUES (Mario,Rossi,Via Garibaldi,Bologna,Moderno)
INSERT INTO CAL_WEEKLY VALUES (21,1,14000,170,300)
SELECT ID,NOME,COGNOME,CITTA FROM EMPLOY WHERE NOME = Mario
