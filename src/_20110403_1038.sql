-- MySQL Administrator dump 1.4
--
-- ------------------------------------------------------
-- Server version	5.1.49-1ubuntu8.1


/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8 */;

/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;


--
-- Create schema DCMTOL
--

CREATE DATABASE IF NOT EXISTS DCMTOL;
USE DCMTOL;

--
-- Definition of table `DCMTOL`.`CONVERSION`
--

DROP TABLE IF EXISTS `DCMTOL`.`CONVERSION`;
CREATE TABLE  `DCMTOL`.`CONVERSION` (
  `outputmime` tinytext NOT NULL,
  `launchline` text NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

--
-- Dumping data for table `DCMTOL`.`CONVERSION`
--

/*!40000 ALTER TABLE `CONVERSION` DISABLE KEYS */;
LOCK TABLES `CONVERSION` WRITE;
INSERT INTO `DCMTOL`.`CONVERSION` VALUES  ('video/webm','gst-launch-0.10 webmmux name=mux ! filesink location=%s uridecodebin uri=%s name=demux demux. ! ffmpegcolorspace ! vp8enc ! queue ! mux.video_0 demux. ! progressreport ! audioconvert !audiorate ! vorbisenc ! queue ! mux.audio_0'),
 ('video/ogg','gst-launch-0.10 oggmux name=\"mux\" ! filesink location=%s uridecodebin uri=%s name=\"d\" { d. ! ffmpegcolorspace ! theoraenc ! queue ! mux. } { d. ! progressreport ! audioconvert ! audiorate ! vorbisenc ! queue ! mux. }');
UNLOCK TABLES;
/*!40000 ALTER TABLE `CONVERSION` ENABLE KEYS */;


--
-- Definition of table `DCMTOL`.`FILES`
--

DROP TABLE IF EXISTS `DCMTOL`.`FILES`;
CREATE TABLE  `DCMTOL`.`FILES` (
  `filename` tinytext NOT NULL,
  `url` text NOT NULL,
  `mime` tinytext NOT NULL,
  `framerate` smallint(6) NOT NULL,
  `width` smallint(6) NOT NULL,
  `height` smallint(6) NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

--
-- Dumping data for table `DCMTOL`.`FILES`
--

/*!40000 ALTER TABLE `FILES` DISABLE KEYS */;
LOCK TABLES `FILES` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `FILES` ENABLE KEYS */;


--
-- Definition of table `DCMTOL`.`MIME_CAPABILITY`
--

DROP TABLE IF EXISTS `DCMTOL`.`MIME_CAPABILITY`;
CREATE TABLE  `DCMTOL`.`MIME_CAPABILITY` (
  `clientip` tinytext NOT NULL,
  `mime` tinytext NOT NULL,
  `useragent` text NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

--
-- Dumping data for table `DCMTOL`.`MIME_CAPABILITY`
--

/*!40000 ALTER TABLE `MIME_CAPABILITY` DISABLE KEYS */;
LOCK TABLES `MIME_CAPABILITY` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `MIME_CAPABILITY` ENABLE KEYS */;


--
-- Definition of table `DCMTOL`.`TIMEOUT`
--

DROP TABLE IF EXISTS `DCMTOL`.`TIMEOUT`;
CREATE TABLE  `DCMTOL`.`TIMEOUT` (
  `clientip` tinytext NOT NULL,
  `timeupdate` bigint(20) unsigned NOT NULL,
  `useragent` text NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

--
-- Dumping data for table `DCMTOL`.`TIMEOUT`
--

/*!40000 ALTER TABLE `TIMEOUT` DISABLE KEYS */;
LOCK TABLES `TIMEOUT` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `TIMEOUT` ENABLE KEYS */;


--
-- Definition of table `DCMTOL`.`VERSION_COMPATIBILITY`
--

DROP TABLE IF EXISTS `DCMTOL`.`VERSION_COMPATIBILITY`;
CREATE TABLE  `DCMTOL`.`VERSION_COMPATIBILITY` (
  `clientip` tinytext NOT NULL,
  `version` text NOT NULL,
  `useragent` text NOT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

--
-- Dumping data for table `DCMTOL`.`VERSION_COMPATIBILITY`
--

/*!40000 ALTER TABLE `VERSION_COMPATIBILITY` DISABLE KEYS */;
LOCK TABLES `VERSION_COMPATIBILITY` WRITE;
UNLOCK TABLES;
/*!40000 ALTER TABLE `VERSION_COMPATIBILITY` ENABLE KEYS */;




/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
