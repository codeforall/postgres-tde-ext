CREATE EXTENSION pg_tde;

CREATE TABLE src (f1 TEXT STORAGE EXTERNAL) USING pg_tde;
INSERT INTO src VALUES(repeat('abcdeF',1000));
SELECT * FROM src;

DROP TABLE src;

DROP EXTENSION pg_tde;
