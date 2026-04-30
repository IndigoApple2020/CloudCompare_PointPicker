// ##########################################################################
// #                                                                        #
// #                              CLOUDCOMPARE                              #
// #                                                                        #
// #  This program is free software; you can redistribute it and/or modify  #
// #  it under the terms of the GNU General Public License as published by  #
// #  the Free Software Foundation; version 2 or later of the License.      #
// #                                                                        #
// #  This program is distributed in the hope that it will be useful,       #
// #  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
// #  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
// #  GNU General Public License for more details.                          //
// #                                                                        #
// #          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
// #                                                                        #
// ##########################################################################

#include "ccNodeGeneratorDlg.h"

// Qt
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>

// =========================================================================
//  Static helper: build LU.<Station>.<Line>.<Direction> prefix
// =========================================================================

/*static*/ QString ccNodeGeneratorDlg::prefix(const QString& stationCode,
                                               const QString& lineCode,
                                               const QString& direction)
{
	return QString("LU.%1.%2.%3").arg(stationCode, lineCode, direction);
}

// =========================================================================
//  Constructor
// =========================================================================

ccNodeGeneratorDlg::ccNodeGeneratorDlg(QWidget* parent)
    : QDialog(parent)
    , Ui::NodeGeneratorDlg()
{
	setupUi(this);
	setWindowTitle(tr("Node Generator — BatGraph F3"));

	connect(browseButton,       &QPushButton::clicked, this, &ccNodeGeneratorDlg::onBrowseCsv);
	connect(outputBrowseButton, &QPushButton::clicked, this, &ccNodeGeneratorDlg::onBrowseOutputFolder);
	connect(previewButton,      &QPushButton::clicked, this, &ccNodeGeneratorDlg::onPreview);
	connect(generateButton,     &QPushButton::clicked, this, &ccNodeGeneratorDlg::onGenerate);
	connect(closeButton,        &QPushButton::clicked, this, &QDialog::accept);
}

// =========================================================================
//  Slots
// =========================================================================

void ccNodeGeneratorDlg::onBrowseCsv()
{
	const QString path = QFileDialog::getOpenFileName(
	    this, tr("Load Station_Stops_By_Line CSV"),
	    QString(), tr("CSV files (*.csv);;All files (*)"));
	if (path.isEmpty()) return;

	csvPathEdit->setText(path);
	if (!loadFromStationCsv(path))
		csvStatusLabel->setText(tr("<i>Could not parse — fill in manually</i>"));
}

void ccNodeGeneratorDlg::onBrowseOutputFolder()
{
	const QString folder = QFileDialog::getExistingDirectory(
	    this, tr("Select output folder"), outputFolderEdit->text());
	if (!folder.isEmpty())
		outputFolderEdit->setText(folder);
}

void ccNodeGeneratorDlg::onPreview()
{
	QVector<NodeRow> nodes;
	QVector<EdgeRow> edges;
	QString err;
	if (!buildGraph(nodes, edges, err))
	{
		previewText->setPlainText(tr("Error: %1").arg(err));
		return;
	}

	QStringList lines;
	lines << tr("=== %1 nodes ===").arg(nodes.size());
	for (const auto& n : nodes)
		lines << QString("%1  [%2]").arg(n.label, n.nodeType);
	lines << QString();
	lines << tr("=== %1 edges ===").arg(edges.size());
	for (const auto& e : edges)
		lines << QString("%1 → %2  [%3]").arg(e.fromNode, e.toNode, e.edgeType);
	previewText->setPlainText(lines.join('\n'));
}

void ccNodeGeneratorDlg::onGenerate()
{
	const QString folder = outputFolderEdit->text().trimmed();
	if (folder.isEmpty())
	{
		QMessageBox::warning(this, tr("Node Generator"), tr("Please select an output folder."));
		return;
	}

	QVector<NodeRow> nodes;
	QVector<EdgeRow> edges;
	QString err;
	if (!buildGraph(nodes, edges, err))
	{
		QMessageBox::critical(this, tr("Node Generator"), tr("Cannot build graph:\n%1").arg(err));
		return;
	}

	bool ok = writeNodesCsv(folder, nodes) && writeEdgesCsv(folder, edges);
	if (ok)
	{
		const QString stn = stationCodeEdit->text().trimmed().toUpper();
		QMessageBox::information(
		    this, tr("Node Generator"),
		    tr("Generated:\n"
		       "  %1/%2_nodes_generated.csv  (%3 nodes)\n"
		       "  %1/%2_edges_generated.csv  (%4 edges)")
		    .arg(folder, stn).arg(nodes.size()).arg(edges.size()));
	}
	else
	{
		QMessageBox::critical(this, tr("Node Generator"), tr("Failed to write output files."));
	}
}

// =========================================================================
//  Graph builder
// =========================================================================

bool ccNodeGeneratorDlg::buildGraph(QVector<NodeRow>& nodes,
                                     QVector<EdgeRow>& edges,
                                     QString&          errorMsg) const
{
	nodes.clear();
	edges.clear();

	const QString stn = stationCodeEdit->text().trimmed().toUpper();
	const QString line = lineComboBox->currentText().trimmed();
	const QString dir  = directionComboBox->currentText().trimmed();

	if (stn.isEmpty())  { errorMsg = tr("Station code is required."); return false; }
	if (line.isEmpty()) { errorMsg = tr("Line code is required.");    return false; }
	if (dir.isEmpty())  { errorMsg = tr("Direction is required.");    return false; }

	const QString exitsRaw = exitsEdit->text().trimmed();
	if (exitsRaw.isEmpty()) { errorMsg = tr("At least one exit letter is required."); return false; }

	const QStringList exits = exitsRaw.split(',', Qt::SkipEmptyParts);
	QStringList exitLetters;
	for (const QString& e : exits) exitLetters.append(e.trimmed().toUpper());
	if (exitLetters.isEmpty()) { errorMsg = tr("Could not parse exit letters."); return false; }

	const int  levels      = levelsSpinBox->value();
	const bool hasBookingHall = bookingHallCheck->isChecked();
	const bool hasStreetExits = streetExitsCheck->isChecked();

	// LU.STN.LIN.DIR prefix
	const QString pfx = prefix(stn, line, dir);

	// ------------------------------------------------------------------ //
	//  Journey Pattern Link (JPL) node — platform hub
	// ------------------------------------------------------------------ //
	//  Naming: LU.STN.LIN.DIR.5
	const QString jplLabel = pfx + ".5";
	nodes.append({jplLabel, "JourneyPatternLink", {}, {}, {}});

	// TrainFront and TrainRear
	const QString trainFrontLabel = pfx + ".5.F";
	const QString trainRearLabel  = pfx + ".5.R";
	nodes.append({trainFrontLabel, "TrainFront", {}, {}, {}});
	nodes.append({trainRearLabel,  "TrainRear",  {}, {}, {}});
	edges.append({jplLabel, trainFrontLabel, "Path"});
	edges.append({jplLabel, trainRearLabel,  "Path"});

	// ------------------------------------------------------------------ //
	//  Per exit: PlatformExit + vertical chain
	// ------------------------------------------------------------------ //
	// Level numbering: platform = <levels>, street = 0
	// Each level above platform has: Base, Elev (stairs/lift), Top, Con
	// At street level: optional BookingHall (level 1), then StreetExit

	for (const QString& exitLetter : exitLetters)
	{
		// PlatformExit — at the foot of the staircase, platform level
		// e.g. LU.CHL.Cen.W.5A
		const QString platformExitLabel = pfx + ".5" + exitLetter;
		nodes.append({platformExitLabel, "PlatformExit", {}, {}, {}});
		edges.append({jplLabel, platformExitLabel, "Path"});

		// Build vertical chain from platform level up to ground
		QString prevTopLabel = platformExitLabel;

		for (int lev = levels; lev >= 1; --lev)
		{
			// Base node at this level
			// e.g. LU.CHL.Cen.W.A.2.Base
			const QString levPfx = pfx + "." + exitLetter + "." + QString::number(lev);

			const QString baseLabel = levPfx + ".Base";
			const QString topLabel  = levPfx + ".Top";
			const QString conLabel  = levPfx + ".Con";

			nodes.append({baseLabel, "Base", {}, {}, {}});
			nodes.append({topLabel,  "Top",  {}, {}, {}});
			nodes.append({conLabel,  "Con",  {}, {}, {}});

			// Connect bottom of stairs to previous level
			edges.append({prevTopLabel, baseLabel, "Path"});
			// Vertical edge (stairs / lift)
			edges.append({baseLabel, topLabel, "Elev"});
			// Horizontal concourse connection
			edges.append({topLabel, conLabel, "Path"});

			prevTopLabel = conLabel; // next iteration climbs from here

			// At level 1 (just below street), add BookingHall if required
			if (lev == 1 && hasBookingHall)
			{
				// LU.CHL.Cen.W.A.BookingHall
				const QString bhLabel = pfx + "." + exitLetter + ".BookingHall";
				nodes.append({bhLabel, "BookingHall", {}, {}, {}});
				edges.append({conLabel, bhLabel, "Path"});
				prevTopLabel = bhLabel;
			}
		}

		// StreetExit at ground level (level 0)
		if (hasStreetExits)
		{
			// LU.CHL.Cen.W.A.Exit
			const QString exitLabel     = pfx + "." + exitLetter + ".Exit";
			// StreetExit aligned to TfL area ID — named Exit for now, operator renames in F5
			const QString streetExitLabel = pfx + "." + exitLetter + ".StreetExit";

			nodes.append({exitLabel,      "Exit",       {}, {}, {}});
			nodes.append({streetExitLabel,"StreetExit", {}, {}, {}});
			edges.append({prevTopLabel, exitLabel,       "Path"});
			edges.append({exitLabel,    streetExitLabel, "Path"});
		}
	}

	return true;
}

// =========================================================================
//  CSV write helpers
// =========================================================================

bool ccNodeGeneratorDlg::writeNodesCsv(const QString& folder, const QVector<NodeRow>& nodes) const
{
	const QString stn  = stationCodeEdit->text().trimmed().toUpper();
	const QString path = folder + "/" + stn + "_nodes_generated.csv";
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

	QTextStream out(&f);
	out << "label,NodeType,x,y,z\n";
	for (const auto& n : nodes)
		out << n.label << "," << n.nodeType << "," << n.x << "," << n.y << "," << n.z << "\n";
	return true;
}

bool ccNodeGeneratorDlg::writeEdgesCsv(const QString& folder, const QVector<EdgeRow>& edges) const
{
	const QString stn  = stationCodeEdit->text().trimmed().toUpper();
	const QString path = folder + "/" + stn + "_edges_generated.csv";
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

	QTextStream out(&f);
	out << "FromNode,ToNode,EdgeType\n";
	for (const auto& e : edges)
		out << e.fromNode << "," << e.toNode << "," << e.edgeType << "\n";
	return true;
}

// =========================================================================
//  Optional: load from Station_Stops_By_Line CSV
// =========================================================================
//
// Expected columns (case-insensitive):
//   StationCode | LineCode | Direction | PlatformExits | Levels | BookingHall
//
// Only the first data row is used (dialog is per-platform-direction).
// Any missing columns are silently skipped.

bool ccNodeGeneratorDlg::loadFromStationCsv(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

	QTextStream in(&f);

	// Read header
	if (in.atEnd()) return false;
	const QStringList header = in.readLine().split(',');
	if (header.isEmpty()) return false;

	auto colIdx = [&](const QString& name) -> int {
		for (int i = 0; i < header.size(); ++i)
			if (header[i].trimmed().compare(name, Qt::CaseInsensitive) == 0)
				return i;
		return -1;
	};

	const int stnIdx  = colIdx("StationCode");
	const int linIdx  = colIdx("LineCode");
	const int dirIdx  = colIdx("Direction");
	const int extIdx  = colIdx("PlatformExits");
	const int levIdx  = colIdx("Levels");
	const int bhIdx   = colIdx("BookingHall");

	if (in.atEnd()) return false;
	const QStringList cols = in.readLine().split(',');

	auto get = [&](int idx) -> QString {
		return (idx >= 0 && idx < cols.size()) ? cols[idx].trimmed() : QString();
	};

	if (stnIdx >= 0) stationCodeEdit->setText(get(stnIdx).toUpper());

	if (linIdx >= 0)
	{
		const QString lc = get(linIdx);
		const int li = lineComboBox->findText(lc, Qt::MatchFixedString | Qt::MatchCaseSensitive);
		if (li >= 0) lineComboBox->setCurrentIndex(li);
	}

	if (dirIdx >= 0)
	{
		const QString dc = get(dirIdx).toUpper();
		const int di = directionComboBox->findText(dc);
		if (di >= 0) directionComboBox->setCurrentIndex(di);
	}

	if (extIdx >= 0) exitsEdit->setText(get(extIdx));
	if (levIdx >= 0) levelsSpinBox->setValue(get(levIdx).toInt());
	if (bhIdx  >= 0)
	{
		const QString bh = get(bhIdx).toLower();
		bookingHallCheck->setChecked(bh == "y" || bh == "yes" || bh == "true" || bh == "1");
	}

	csvStatusLabel->setText(tr("Loaded from CSV ✓"));
	return true;
}
