REM An apostrophe between two letters or digits is part of the word, in quotes or not
INSERT INTO EMPLOY VALUES (Gabriele,D'Annunzio,Via dell'Orto 3,L'Aquila)
INSERT INTO EMPLOY VALUES ('Anna Maria', 'Dell'Acqua', 'Corso Sant'Angelo', 'Reggio nell''Emilia')
INSERT INTO EMPLOY VALUES (Piero,Nell'800,'Via d'Azeglio, 12',Città dell'Arte)
SELECT * FROM EMPLOY WHERE ID > 68
SELECT ID,COGNOME FROM EMPLOY WHERE CITTA = L'Aquila
SELECT ID,COGNOME FROM EMPLOY WHERE COGNOME = 'Dell'Acqua'
SELECT ID,COGNOME FROM EMPLOY WHERE COGNOME = 'Dell''Acqua'
SELECT ID,VIA FROM EMPLOY WHERE VIA LIKE '%sant'a%'
UPDATE EMPLOY SET CITTA = Sant'Agata WHERE COGNOME = D'Annunzio
SELECT ID,CITTA FROM EMPLOY WHERE ID = 69; SELECT 'it''s', COGNOME FROM EMPLOY WHERE ID = 70
REM Errors: a value with spaces needs quotes; an apostrophe at the end of a word is written twice
SELECT ID FROM EMPLOY WHERE VIA = Via dell'Orto 3
SELECT ID FROM EMPLOY WHERE CITTA = 'Ca' Foscari'
SELECT ID FROM EMPLOY WHERE CITTA = 'Ca'' Foscari'
SELECT ID FROM EMPLOY WHERE CITTA = Roma OR CITTA = Torino
