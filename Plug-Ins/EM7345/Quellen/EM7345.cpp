/*
	Copyright (C) 2015 Frank Büttner frank@familie-büttner.de

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>
*/


#include "EM7345.h"
#include "Vorgaben.h"

#include <QtSerialPort>

#include <syslog.h>
#include <cmath>


/*! \class EM7345Plugin Erweiterung_EM7345.h
	\brief Die Erweiterung für die Pluginfabrik.
*/

/*! \class Erweiterung_EM7345 Erweiterung_EM7345.h
	\brief Die Erweiterung für die Steuerung.
*/

/*! \class EM7345 EM7345.h
	\brief Das Modul für das %EM735 LTE Modem.

	Getestet wurde es mit der Firmware 1.1.
*/

/*!
	\fn EM7345::Daten(const QString &daten)
	\brief Teilt der Steuerung mit, das neue Koordinaten empfangen worden sind.

	Die Übermittlung erfolgt als NMEA Datensatz.
	\param[out] daten Der NMEA Datensatz.
*/

/*!
	\fn EM7345::MeldungSenden(Meldung meldung)
	\brief Teilt der Steuerung mit, wenn eine %Meldung vorliegt.

	Hierüber werden Debug-, Fehler- und Informationsmeldungen verschickt.
	\param[out] meldung Die zu sendende %Meldung.
*/

/*!
	\fn EM7345::Beendet()
	\brief Teilt der Steuerung mit, wenn die Hardware abgeschaltet ist.
*/

/*!
  \brief Erstelllt das %EM7345 Objekt.
  \param[in,out] eltern Das Elternobjekt.
  \param[in] konfiguration Die Konfiguration für das Programm
*/
EM7345::EM7345(QObject *eltern, const QSettings *konfiguration) : QObject(eltern)
{
	QTranslator Uebersetzung;
	Uebersetzung.load(QString("%1_%2").arg(NAME).arg(QLocale::system().name()),QLibraryInfo::location(QLibraryInfo::TranslationsPath));
	if((Uebersetzung.isEmpty()) && (!QLocale::system().name().startsWith("de")))
		Uebersetzung.load(QString("%1_en").arg(NAME),QLibraryInfo::location(QLibraryInfo::TranslationsPath));
	QCoreApplication::installTranslator(&Uebersetzung);

	K_Konfiguration=konfiguration;
	K_Anschluss=K_Konfiguration->value("EM7345/Anschluss",ANSCHLUSS).toString();
	K_IDGesetzt=false;
	K_Datenwachhund=new QTimer(this);
	K_Datenwachhund->setInterval(WARTEN_AUF_DATEN*1000);
	connect(K_Datenwachhund,SIGNAL(timeout()),this,SLOT(KeineDatenBekommen()));
	QTimer::singleShot(0,this,SLOT(starten()));
}

/*!
	\brief Beendet die GPS Hardware.

	Weist das Modem an, den GPS Empfänger abzuschalten.
*/
void EM7345::Beenden()
{
	if(K_Modem->isOpen())
	{
		if(K_IDGesetzt)
		{
			K_Datenwachhund->stop();
			K_Modem->write(QString("AT+XLSRSTOP=0,%1\r").arg(K_ID).toLocal8Bit());
			Q_EMIT MeldungSenden(Meldung("113aae59435d470bb9673009708746ba",tr("%1 stoppe GPS").arg(NAME),LOG_DEBUG));
		}
	}
}

void EM7345::starten()
{
	Q_EMIT MeldungSenden(Meldung("a42c182ded374fcb86fd8bd605d9cfa6",tr("%1 benutze Anschluss %2").arg(NAME).arg(K_Anschluss),LOG_INFO));
	K_Modem =new QSerialPort(this);
	connect(K_Modem,SIGNAL(readyRead()),this,SLOT(DatenZumLesen()));
	K_Modem->setPortName(K_Anschluss);
	if(!K_Modem->setBaudRate(QSerialPort::Baud115200))
	{
		Q_EMIT MeldungSenden(Meldung("e6feee5d8f964dae9861a094e15014a6",tr("%1 Kann die Baudrate nicht setzen.\n%2").arg(NAME).arg(K_Modem->errorString()),LOG_CRIT));
		return;
	}
	if(!K_Modem->open(QIODevice::ReadWrite))
	{
		Q_EMIT MeldungSenden(Meldung("4cae08cb4a704affa343a6911deb009d",trUtf8("%1 Kann den Anschluss nicht öffnen.\n%2").arg(NAME).arg(K_Modem->errorString()),LOG_CRIT));
		return;
	}
	K_Modem->write("AT+XLCSLSR=1,1,,,,,1,,,,,\r");
	K_Datenwachhund->start();
}
void EM7345::DatenZumLesen()
{
	QString Daten=K_Modem->readAll().simplified();
	Q_EMIT MeldungSenden(Meldung("70ebb46432b041d9b7aeb56356ef81e6",tr("%1 Daten empfangen: %2").arg(NAME).arg(Daten),LOG_DEBUG));
	//Manchmal sendet das Modem Müll
	if(Daten.startsWith("OK"))
		return;
	//Kennen wir die Meldung?
	if( (!Daten.startsWith("+XLCSLSR:")) && (!Daten.startsWith("ERROR")) && (!Daten.startsWith("+XLSRSTOP:")) )
	{
		Q_EMIT MeldungSenden(Meldung("da93a946ab2f4b3fa1a9af2bcdb8f101",tr("%1 Nicht erwartete Daten: %2").arg(NAME).arg(Daten),LOG_WARNING));
		return;
	}
	if(!K_IDGesetzt)
	{
		if (!Daten.contains("+XLCSLSR: request id"))
		{
			Q_EMIT MeldungSenden(Meldung("da6cd0ec261540cbb1a5c10443189a77",tr("%1 Konnte keine Komando ID bekommen.").arg(NAME),LOG_CRIT));
			return;
		}
		QString tmp=Daten.mid(Daten.indexOf("id")+3,Daten.size()-Daten.indexOf("OK"));
		tmp=tmp.simplified();
		K_IDGesetzt=true;
		K_ID=tmp.toInt();
		Q_EMIT MeldungSenden(Meldung("a8bf50df94134c5793127e0b37c96c51",QString("%1 KomandoID: %2").arg(NAME).arg(K_ID),LOG_DEBUG));
		return;
	}
	if(Daten.contains("+XLSRSTOP: OK"))
	{
		K_IDGesetzt=false;
		Q_EMIT MeldungSenden(Meldung("0c883885b2c349cfb69f6b18b921b25b",tr("%1 GPS gestoppt.").arg(NAME),LOG_DEBUG));
		Q_EMIT Beendet();
		return;
	}
	K_Datenwachhund->stop();
	if(Daten.contains("ERROR"))
	{
		Q_EMIT MeldungSenden(Meldung("a51315bd6fdc437e99535fe638a61468",tr("%1 meldet ein Fehler: %2").arg(NAME).arg(Daten),LOG_ERR));
		return;
	}
	QStringList Liste=Daten.split(",");
	//Keine Lehrzeichen
	for(int Eintrag=0;Eintrag<Liste.size();Eintrag++)
		Liste[Eintrag]=Liste[Eintrag].simplified();
	//Modem Status
	Liste[0]=Liste[0].split(" ").at(1);
	//Letzer Eintrag vom ATR Status befreien.
	Liste[Liste.size()-1]=Liste[Liste.size()-1].split(" ").at(0);

	/*
		01	Breite -> muss aber konvertiert werden.
		02	Länge -> muss aber konvertiert werden.
		09	Datum JJJJ/MM/TT
		10  Zeit SS:MM:ss
	 */
	QString Breite=Liste[1];
	QString Laenge=Liste[2];
	QString NMEA_Breite,NMEA_Laenge;
	QChar BreiteRichtung;
	QChar LaengeRichtung;
	int Grad;
	double Minutenanteil,Minuten;
	QDateTime DatumZeit=QDateTime::fromString(QString("%1 %2").arg(Liste[9]).arg(Liste[10]),"yyyy/MM/dd hh:mm:ss");
	DatumZeit.setTimeZone(QTimeZone::utc());
	BreiteRichtung=Breite[Breite.size()-1];
	LaengeRichtung=Laenge[Laenge.size()-1];
	Breite=Breite.left(Breite.size()-2);
	Laenge=Laenge.left(Laenge.size()-2);
	/*
	 NMEA GGMM.mmmm
	 Die Gradzahl entpsicht der Stelle vor dem Komma.
	 Minuten der ganzahlige Anteil.
	 Minutenanzteil ist der Rest des Minutenanteils
	 */
	Grad=std::floor(Breite.toDouble());
	Minutenanteil=std::modf(((Breite.toDouble()-Grad)*60.0),&Minuten);
	NMEA_Breite=QString("%1%2.%3").arg(Grad).arg(Minuten).arg(Minutenanteil);
	qWarning()<<NMEA_Breite;

}
void EM7345::KeineDatenBekommen()
{
	Q_EMIT MeldungSenden(Meldung("d6c664665e74468d85a321b20ea6b4c1",tr("%1 Keine Daten empfangen").arg(NAME),LOG_ERR));
}
