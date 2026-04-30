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
/** F8a: read-only viewer with Fruchterman-Reingold layout.
    F8b: adds edit mode — drag nodes, draw edges, add/delete elements,
         save changes back to the originating CSV files.
 **/
class ccGraphPanelDlg : public QDialog, public Ui::GraphPanelDlg
{
	Q_OBJECT

  public:
	explicit ccGraphPanelDlg(QWidget* parent = nullptr);

	//! Reload previously loaded files
	void reload();

	// ---- Called by interactive scene items ----
	void showNodeInspector(int nodeIdx);
	void showEdgeInspector(int edgeIdx);
	//! Begin drawing an edge from nodeIdx (edit mode, right-click on node)
	void startEdgeFrom(int nodeIdx);
	//! Complete edge to nodeIdx (edit mode, click on target node while drawing)
	void completeEdgeTo(int toIdx);
	//! Cancel in-progress edge draw
	void cancelEdgeDraw();
	//! Delete node by index
	void deleteNode(int nodeIdx);
	//! Delete edge by index
	void deleteEdge(int edgeIdx);
	//! Add a new node at the given scene position (edit mode, click on empty canvas)
	void addNodeAt(const QPointF& scenePos);
	//! Update rubber-band edge endpoint during edge-drawing
	void updateRubberEdge(const QPointF& scenePos);
	//! Sync a node's position after a drag (called by NodeItem on mouse release)
	void syncNodePosition(int nodeIdx, const QPointF& scenePos);
	//! Change edge type via dialog (called by EdgeItem context menu)
	void changeEdgeType(int edgeIdx);

	bool isEditMode()    const { return m_editMode; }
	bool isDrawingEdge() const { return m_drawingEdge; }

  private slots:
	void onLoadNodes();
	void onLoadEdges();
	void onRelayout();
	void onFitView();
	void onToggleEditMode(bool checked);
	void onSaveGraph();

  private:
	// ------------------------------------------------------------------ //
	//  Data model                                                          //
	// ------------------------------------------------------------------ //

	struct GraphNode
	{
		QString                label;
		QString                nodeType;
		QMap<QString, QString> attrs;
		double                 x = 0.0;
		double                 y = 0.0;
		QGraphicsEllipseItem*  item       = nullptr;
		QGraphicsItem*         label_item = nullptr;
	};

	struct GraphEdge
	{
		QString                fromLabel;
		QString                toLabel;
		QString                edgeType;
		QMap<QString, QString> attrs;
		QGraphicsLineItem*     item = nullptr;
	};

	// ------------------------------------------------------------------ //
	//  Internal helpers                                                    //
	// ------------------------------------------------------------------ //

	bool loadNodesCsv(const QString& path);
	bool loadEdgesCsv(const QString& path);

	void rebuildScene();
	void rebuildSceneItemsOnly(); // recreate graphics without re-running layout
	void runFruchtermanReingold();
	void createNodeItems();
	void createEdgeItems();
	void updateStatusLabels();
	void markDirty();

	// Edit helpers (internal — no dirty/status update)
	void deleteEdgeInternal(int edgeIdx);

	// Colour helpers (static)
	static QColor  nodeColour(const QString& nodeType);
	static QColor  edgeColour(const QString& edgeType);
	static bool    isUnlabelled(const QString& label);
	static QString deriveNodeType(const QString& label, const QString& csvNodeType);

	// CSV write-back
	void saveNodesCsv() const;
	void saveEdgesCsv() const;

	// ------------------------------------------------------------------ //
	//  Members                                                             //
	// ------------------------------------------------------------------ //

	QGraphicsView*  m_view  = nullptr;
	QGraphicsScene* m_scene = nullptr;

	QString m_nodesCsvPath;
	QString m_edgesCsvPath;

	QVector<GraphNode> m_nodes;
	QVector<GraphEdge> m_edges;
	QMap<QString, int> m_nodeIndex; // label → index

	// Edit state
	bool               m_editMode    = false;
	bool               m_drawingEdge = false;
	int                m_edgeSrcIdx  = -1;
	QGraphicsLineItem* m_rubberEdge  = nullptr;
	bool               m_dirty       = false;
};

#endif
