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

#ifndef CC_GRAPH_PANEL_DIALOG_HEADER
#define CC_GRAPH_PANEL_DIALOG_HEADER

#include <ui_graphPanelDlg.h>

#include <QDialog>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWheelEvent>

//! 2-D force-directed graph panel (BatGraph F8)
/** Loads nodes and edges from CSV files, applies Fruchterman-Reingold layout,
    and renders the result in a pannable/zoomable QGraphicsView.
    Selecting a node or edge shows its attributes in the inspector panel.
 **/
class ccGraphPanelDlg : public QDialog, public Ui::GraphPanelDlg
{
	Q_OBJECT

  public:
	explicit ccGraphPanelDlg(QWidget* parent = nullptr);

	//! Reload previously loaded files (e.g. called after external edit)
	void reload();

	//! Show node attributes in inspector (called by NodeItem on click)
	void showNodeInspector(int nodeIdx);
	//! Show edge attributes in inspector (called by EdgeItem on click)
	void showEdgeInspector(int edgeIdx);

  private slots:
	void onLoadNodes();
	void onLoadEdges();
	void onRelayout();
	void onFitView();

  private:
	// ------------------------------------------------------------------ //
	//  Data model                                                          //
	// ------------------------------------------------------------------ //

	struct GraphNode
	{
		QString             label;
		QString             nodeType; // derived or from CSV
		QMap<QString, QString> attrs; // all columns
		double              x = 0.0; // layout position
		double              y = 0.0;
		// Graphics item (owned by scene)
		QGraphicsEllipseItem* item  = nullptr;
		QGraphicsItem*        label_item = nullptr;
	};

	struct GraphEdge
	{
		QString             fromLabel;
		QString             toLabel;
		QString             edgeType;
		QMap<QString, QString> attrs;
		// Graphics item (owned by scene)
		QGraphicsLineItem*  item = nullptr;
	};

	// ------------------------------------------------------------------ //
	//  Internal helpers                                                    //
	// ------------------------------------------------------------------ //

	bool loadNodesCsv(const QString& path);
	bool loadEdgesCsv(const QString& path);

	void rebuildScene();
	void runFruchtermanReingold();
	void createNodeItems();
	void createEdgeItems();
	void updateStatusLabels();

	// Colour helpers
	static QColor nodeColour(const QString& nodeType);
	static QColor edgeColour(const QString& edgeType);
	static bool   isUnlabelled(const QString& label);
	static QString deriveNodeType(const QString& label, const QString& csvNodeType);

	// ------------------------------------------------------------------ //
	//  Members                                                             //
	// ------------------------------------------------------------------ //

	QGraphicsView*  m_view  = nullptr;
	QGraphicsScene* m_scene = nullptr;

	QString m_nodesCsvPath;
	QString m_edgesCsvPath;

	QVector<GraphNode> m_nodes;
	QVector<GraphEdge> m_edges;
	// Maps label -> index in m_nodes for quick lookup
	QMap<QString, int> m_nodeIndex;
};

#endif
