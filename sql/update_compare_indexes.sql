CREATE EXTENSION pg_tde;

SELECT pg_tde_add_key_provider_file('file-valut','/tmp/pg_tde_test_keyring.per');
--SELECT pg_tde_set_master_key('test-db-master-key','file-valut');

DROP TABLE IF EXISTS pvactst;
CREATE TABLE pvactst (i INT, a INT[], p POINT) USING pg_tde;
INSERT INTO pvactst SELECT i, array[1,2,3], point(i, i+1) FROM generate_series(1,1000) i;
CREATE INDEX spgist_pvactst ON pvactst USING spgist (p);
UPDATE pvactst SET i = i WHERE i < 1000;
-- crash!

DROP TABLE pvactst;
DROP EXTENSION pg_tde;
