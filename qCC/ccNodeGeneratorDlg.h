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
// #  GNU General Public License for more details.                          #
// #                                                                        #
// #          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
// #                                                                        #
// ##########################################################################

#ifndef CC_NODE_GENERATOR_DIALOG_HEADER
#define CC_NODE_GENERATOR_DIALOG_HEADER

#include <ui_nodeGeneratorDlg.h>

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

//! Generates a standard within-station node/edge topology (BatGraph F3)
/** Given a station code, line, direction, exit list, and level count,
    produces nodes.csv and edges.csv covering:
      - Journey Pattern Link node
      - TrainFront / TrainRear anchors
      - PlatformExit nodes (.A, .B, …) at platform level
      - Per exit: vertical chain (Base → Elev → Top → Con) per level
      - Optional BookingHall at level 1
      - Optional StreetExit leaf at level 0 per exit

    Optionally pre-populates station / line / direction from a
    Station_Stops_By_Line.csv export of LU_Interchanges.xlsx.
 **/
class ccNodeGeneratorDlg : public QDialog, public Ui::NodeGeneratorDlg
{
	Q_OBJECT

  public:
	explicit ccNodeGeneratorDlg(QWidget* parent = nullptr);

	//! Internal node row
	struct NodeRow
	{
		QString label;
		QString nodeType;
		QString x, y, z; // blank initially — user places in 3D later
	};

	//! Internal edge row
	struct EdgeRow
	{
		QString fromNode;
		QString toNode;
		QString edgeType;
	};

  private slots:
	void onBrowseCsv();
	void onBrowseOutputFolder();
	void onPreview();
	void onGenerate();

  private:
	//! Build node and edge lists from current UI values
	bool buildGraph(QVector<NodeRow>& nodes, QVector<EdgeRow>& edges, QString& errorMsg) const;

	//! Write nodes to <folder>/<stationCode>_nodes_generated.csv
	bool writeNodesCsv(const QString& folder, const QVector<NodeRow>& nodes) const;

	//! Write edges to <folder>/<stationCode>_edges_generated.csv
	bool writeEdgesCsv(const QString& folder, const QVector<EdgeRow>& edges) const;

	//! Load first matching row from Station_Stops_By_Line CSV and populate UI
	bool loadFromStationCsv(const QString& path);

	// Helpers
	static QString prefix(const QString& stationCode,
	                       const QString& lineCode,
	                       const QString& direction);
};

#endif
