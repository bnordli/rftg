# Keldon's RFTG MySQL tables (extracted from server.c)
CREATE DATABASE rftg;

USE rftg;

CREATE USER 'rftg'@'localhost';

GRANT SELECT,INSERT,UPDATE,DELETE ON rftg.* TO 'rftg'@'localhost';

# All blob and text variables have max size 1024
CREATE TABLE users(
 uid INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
 user TEXT NOT NULL,
 pass TEXT NOT NULL);

CREATE TABLE games(
 gid INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
 description TEXT NOT NULL,
 pass TEXT NOT NULL,
 created INT NOT NULL,
 state ENUM('WAITING','STARTED','DONE','ABANDONED'),
 minp INT NOT NULL,
 maxp INT NOT NULL,
 exp INT NOT NULL,
 adv INT NOT NULL,
 dis_goal INT NOT NULL,
 dis_takeover INT NOT NULL,
 variant INT NOT NULL, # Only used in version 0.8.1n
 speed INT NOT NULL,
 version TEXT NOT NULL);

CREATE TABLE attendance(
 uid INT NOT NULL,
 gid INT NOT NULL,
 ai INT NOT NULL,
 seat INT NOT NULL,
 waiting ENUM('READY','BLOCKED','OPTION')); # From version 0.8.1m

# Note: the column names are not present in the code
CREATE TABLE results(
 gid INT NOT NULL,
 uid INT NOT NULL,
 vp INT NOT NULL,
 tie INT NOT NULL,
 winner INT NOT NULL);

CREATE TABLE seed(
 gid INT NOT NULL,
 pool BLOB NOT NULL);

# Exception: log is max length 20000
CREATE TABLE choices(
 gid INT NOT NULL,
 uid INT NOT NULL,
 log BLOB NOT NULL,
 PRIMARY KEY (gid, uid));
 
# This table is only used from version 0.8.1k
CREATE TABLE messages(
 mid INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
 gid INT NOT NULL,
 uid INT NOT NULL,
 message TEXT NOT NULL,
 format CHAR(16) NOT NULL);
