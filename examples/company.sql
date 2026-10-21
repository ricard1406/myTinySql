REM Sample rows for the COMPANY table (ID, NAME, TYPE, ADDRESS, TOWN).
REM The companies are made up. Run once on an empty data/COMPANY_DATA:
REM   touch data/COMPANY_DATA && ./myTinySql --sqlfile=examples/company.sql
INSERT INTO COMPANY VALUES (Rossi Costruzioni,Construction,Via Garibaldi 12,Torino)
INSERT INTO COMPANY VALUES (Bianchi Software,Software,Corso Venezia 45,Milano)
INSERT INTO COMPANY VALUES (Verdi Trasporti,Transport,Via Emilia 230,Bologna)
INSERT INTO COMPANY VALUES (Panificio Esposito,Bakery,Via Toledo 88,Napoli)
INSERT INTO COMPANY VALUES (Studio Ferrari,Architecture,Piazza Bra 3,Verona)
INSERT INTO COMPANY VALUES (Gallo Elettronica,Electronics,Via Nazionale 150,Roma)
INSERT INTO COMPANY VALUES (Cantine Moretti,Winery,Strada del Vino 7,Firenze)
INSERT INTO COMPANY VALUES (Romano Assicurazioni,Insurance,Corso Italia 21,Genova)
SELECT * FROM COMPANY
