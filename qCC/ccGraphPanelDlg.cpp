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

#include "ccGraphPanelDlg.h"

// Qt
#include <QFile>
#include <QFileDialog>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtMath>

// =========================================================================
//  GraphScene — captures canvas-level mouse/key events for edit mode
// =========================================================================

class GraphScene : public QGraphicsScene
{
  public:
	explicit GraphScene(ccGraphPanelDlg* dlg, QObject* parent = nullptr)
	    : QGraphicsScene(parent), m_dlg(dlg)
	{
	}

  protected:
	void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override
	{
		if (m_dlg->isEditMode() && m_dlg->isDrawingEdge())
			m_dlg->updateRubberEdge(event->scenePos());
		QGraphicsScene::mouseMoveEvent(event);
	}

	void mousePressEvent(QGraphicsSceneMouseEvent* event) override
	{
		if (m_dlg->isEditMode())
		{
			if (m_dlg->isDrawingEdge())
			{
				// Click on empty space while drawing → cancel
				if (!itemAt(event->scenePos(), QTransform()))
				{
					m_dlg->cancelEdgeDraw();
					return;
				}
				// Click on an item — let the item handle completeEdgeTo
			}
			else if (event->button() == Qt::LeftButton)
			{
				// Left-click on empty canvas → add new node
				if (!itemAt(event->scenePos(), QTransform()))
				{
					m_dlg->addNodeAt(event->scenePos());
					return;
				}
			}
		}
		QGraphicsScene::mousePressEvent(event);
	}

	void keyPressEvent(QKeyEvent* event) override
	{
		if (m_dlg->isEditMode() && event->key() == Qt::Key_Escape)
		{
			m_dlg->cancelEdgeDraw();
			return;
		}
		QGraphicsScene::keyPressEvent(event);
	}

  private:
	ccGraphPanelDlg* m_dlg;
};

// =========================================================================
//  GraphView — wheel-zoom and pan
// =========================================================================

class GraphView : public QGraphicsView
{
  public:
	explicit GraphView(QWidget* parent = nullptr) : QGraphicsView(parent)
	{
		setRenderHint(QPainter::Antialiasing, true);
		setDragMode(QGraphicsView::ScrollHandDrag);
		setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
		setResizeAnchor(QGraphicsView::AnchorViewCenter);
		setBackgroundBrush(QColor(30, 30, 30));
	}

  protected:
	void wheelEvent(QWheelEvent* event) override
	{
		const double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
		scale(factor, factor);
		event->accept();
	}
};

// =========================================================================
//  NodeItem
// =========================================================================

class NodeItem : public QGraphicsEllipseItem
{
  public:
	NodeItem(int nodeIdx, ccGraphPanelDlg* dlg, const QRectF& rect, QGraphicsItem* parent = nullptr)
	    : QGraphicsEllipseItem(rect, parent), m_nodeIdx(nodeIdx), m_dlg(dlg)
	{
		setFlag(QGraphicsItem::ItemIsSelectable, true);
		setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
		setAcceptHoverEvents(true);
	}

	void setDraggable(bool on) { setFlag(QGraphicsItem::ItemIsMovable, on); }

  protected:
	void mousePressEvent(QGraphicsSceneMouseEvent* event) override
	{
		if (event->button() == Qt::LeftButton)
		{
			if (m_dlg->isEditMode() && m_dlg->isDrawingEdge())
			{
				m_dlg->completeEdgeTo(m_nodeIdx);
				event->accept();
				return;
			}
			m_dlg->showNodeInspector(m_nodeIdx);
		}
		QGraphicsEllipseItem::mousePressEvent(event);
	}

	void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
	{
		QGraphicsEllipseItem::mouseReleaseEvent(event);
		if (m_dlg->isEditMode())
			m_dlg->syncNodePosition(m_nodeIdx, pos());
	}

	void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override
	{
		if (!m_dlg->isEditMode()) return;
		QMenu menu;
		QAction* drawAct   = menu.addAction(tr("Draw edge from here"));
		QAction* deleteAct = menu.addAction(tr("Delete node"));
		QAction* chosen    = menu.exec(event->screenPos());
		if (chosen == drawAct)   m_dlg->startEdgeFrom(m_nodeIdx);
		if (chosen == deleteAct) m_dlg->deleteNode(m_nodeIdx);
		event->accept();
	}

	void hoverEnterEvent(QGraphicsSceneHoverEvent*) override { setOpacity(0.75); }
	void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override { setOpacity(1.0); }

  private:
	int              m_nodeIdx;
	ccGraphPanelDlg* m_dlg;
};

// =========================================================================
//  EdgeItem
// =========================================================================

class EdgeItem : public QGraphicsLineItem
{
  public:
	EdgeItem(int edgeIdx, ccGraphPanelDlg* dlg, QGraphicsItem* parent = nullptr)
	    : QGraphicsLineItem(parent), m_edgeIdx(edgeIdx), m_dlg(dlg)
	{
		setFlag(QGraphicsItem::ItemIsSelectable, true);
	}

  protected:
	void mousePressEvent(QGraphicsSceneMouseEvent* event) override
	{
		QGraphicsLineItem::mousePressEvent(event);
		if (event->button() == Qt::LeftButton)
			m_dlg->showEdgeInspector(m_edgeIdx);
	}

	void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override
	{
		if (!m_dlg->isEditMode()) return;
		QMenu menu;
		QAction* changeAct = menu.addAction(tr("Change edge type…"));
		QAction* deleteAct = menu.addAction(tr("Delete edge"));
		QAction* chosen    = menu.exec(event->screenPos());
		if (chosen == changeAct) m_dlg->changeEdgeType(m_edgeIdx);
		if (chosen == deleteAct) m_dlg->deleteEdge(m_edgeIdx);
		event->accept();
	}

  private:
	int              m_edgeIdx;
	ccGraphPanelDlg* m_dlg;
};

// =========================================================================
//  Column helpers
// =========================================================================

static int findColIdx(const QStringList& header, const QString& name)
{
	for (int i = 0; i < header.size(); ++i)
		if (header[i].trimmed().compare(name, Qt::CaseInsensitive) == 0)
			return i;
	return -1;
}

static QString safeCol(const QStringList& cols, int idx)
{
	return (idx >= 0 && idx < cols.size()) ? cols[idx].trimmed() : QString();
}

// =========================================================================
//  ccGraphPanelDlg — constructor
// =========================================================================

ccGraphPanelDlg::ccGraphPanelDlg(QWidget* parent)
    : QDialog(parent, Qt::Window)
    , Ui::GraphPanelDlg()
{
	setupUi(this);
	setWindowTitle(tr("Graph Panel — BatGraph F8"));

	m_scene = new GraphScene(this, this);
	m_view  = new GraphView(graphContainer);
	m_view->setScene(m_scene);

	auto* lay = new QVBoxLayout(graphContainer);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->addWidget(m_view);

	connect(loadNodesButton, &QPushButton::clicked,  this, &ccGraphPanelDlg::onLoadNodes);
	connect(loadEdgesButton, &QPushButton::clicked,  this, &ccGraphPanelDlg::onLoadEdges);
	connect(relayoutButton,  &QPushButton::clicked,  this, &ccGraphPanelDlg::onRelayout);
	connect(fitButton,       &QPushButton::clicked,  this, &ccGraphPanelDlg::onFitView);
	connect(editModeButton,  &QPushButton::toggled,  this, &ccGraphPanelDlg::onToggleEditMode);
	connect(saveGraphButton, &QPushButton::clicked,  this, &ccGraphPanelDlg::onSaveGraph);
	connect(closeButton,     &QPushButton::clicked,  this, &QDialog::accept);
}

// =========================================================================
//  Public API
// =========================================================================

void ccGraphPanelDlg::reload()
{
	if (!m_nodesCsvPath.isEmpty()) loadNodesCsv(m_nodesCsvPath);
	if (!m_edgesCsvPath.isEmpty()) loadEdgesCsv(m_edgesCsvPath);
	rebuildScene();
}

void ccGraphPanelDlg::showNodeInspector(int nodeIdx)
{
	if (nodeIdx < 0 || nodeIdx >= m_nodes.size()) return;
	const GraphNode& n = m_nodes[nodeIdx];
	inspectorTitleLabel->setText(tr("<b>Node:</b> %1").arg(n.label.toHtmlEscaped()));
	QString text;
	for (auto it = n.attrs.cbegin(); it != n.attrs.cend(); ++it)
		text += it.key() + " = " + it.value() + "\n";
	inspectorText->setPlainText(text.trimmed());
}

void ccGraphPanelDlg::showEdgeInspector(int edgeIdx)
{
	if (edgeIdx < 0 || edgeIdx >= m_edges.size()) return;
	const GraphEdge& e = m_edges[edgeIdx];
	inspectorTitleLabel->setText(
	    tr("<b>Edge:</b> %1 → %2").arg(e.fromLabel.toHtmlEscaped(), e.toLabel.toHtmlEscaped()));
	QString text;
	for (auto it = e.attrs.cbegin(); it != e.attrs.cend(); ++it)
		text += it.key() + " = " + it.value() + "\n";
	inspectorText->setPlainText(text.trimmed());
}

// =========================================================================
//  Edit mode — edge drawing
// =========================================================================

void ccGraphPanelDlg::startEdgeFrom(int nodeIdx)
{
	if (nodeIdx < 0 || nodeIdx >= m_nodes.size()) return;
	cancelEdgeDraw();
	m_drawingEdge = true;
	m_edgeSrcIdx  = nodeIdx;

	const QPointF srcPos(m_nodes[nodeIdx].x, m_nodes[nodeIdx].y);
	m_rubberEdge = new QGraphicsLineItem(QLineF(srcPos, srcPos));
	m_rubberEdge->setPen(QPen(Qt::yellow, 1.5, Qt::DashLine));
	m_rubberEdge->setZValue(10.0);
	m_scene->addItem(m_rubberEdge);
	editHintLabel->setText(tr("<i>Click target node to complete,<br>or Esc to cancel</i>"));
}

void ccGraphPanelDlg::completeEdgeTo(int toIdx)
{
	if (!m_drawingEdge || toIdx < 0 || toIdx >= m_nodes.size()) return;
	const int fromIdx = m_edgeSrcIdx;
	cancelEdgeDraw();
	if (fromIdx == toIdx) return;

	const QStringList types = {"Path", "Elev", "STAIRS", "Float0", "JPL", "TempEdge"};
	bool ok = false;
	const QString edgeType = QInputDialog::getItem(
	    this, tr("Edge type"), tr("Type for new edge:"), types, 0, false, &ok);
	if (!ok) return;

	GraphEdge edge;
	edge.fromLabel = m_nodes[fromIdx].label;
	edge.toLabel   = m_nodes[toIdx].label;
	edge.edgeType  = edgeType;
	edge.attrs.insert("FromNode", edge.fromLabel);
	edge.attrs.insert("ToNode",   edge.toLabel);
	edge.attrs.insert("EdgeType", edgeType);
	m_edges.append(edge);

	const GraphNode& nF = m_nodes[fromIdx];
	const GraphNode& nT = m_nodes[toIdx];
	const bool dashed = (edgeType.toLower() == "float0" || edgeType.toLower() == "tempedge");
	QPen pen(edgeColour(edgeType), dashed ? 1.0 : 1.5);
	if (dashed) pen.setStyle(Qt::DashLine);

	auto* item = new EdgeItem(m_edges.size() - 1, this);
	item->setLine(nF.x, nF.y, nT.x, nT.y);
	item->setPen(pen);
	item->setZValue(1.0);
	m_scene->addItem(item);
	m_edges.last().item = item;

	updateStatusLabels();
	markDirty();
}

void ccGraphPanelDlg::cancelEdgeDraw()
{
	if (m_rubberEdge) { m_scene->removeItem(m_rubberEdge); delete m_rubberEdge; m_rubberEdge = nullptr; }
	m_drawingEdge = false;
	m_edgeSrcIdx  = -1;
	editHintLabel->clear();
}

void ccGraphPanelDlg::updateRubberEdge(const QPointF& scenePos)
{
	if (!m_rubberEdge) return;
	QLineF line = m_rubberEdge->line();
	line.setP2(scenePos);
	m_rubberEdge->setLine(line);
}

void ccGraphPanelDlg::syncNodePosition(int nodeIdx, const QPointF& pos)
{
	if (nodeIdx < 0 || nodeIdx >= m_nodes.size()) return;
	m_nodes[nodeIdx].x = pos.x();
	m_nodes[nodeIdx].y = pos.y();

	// Update connected edge items
	const QString& lbl = m_nodes[nodeIdx].label;
	for (auto& edge : m_edges)
	{
		if (!edge.item) continue;
		if (edge.fromLabel != lbl && edge.toLabel != lbl) continue;
		auto itF = m_nodeIndex.find(edge.fromLabel);
		auto itT = m_nodeIndex.find(edge.toLabel);
		if (itF != m_nodeIndex.end() && itT != m_nodeIndex.end())
			edge.item->setLine(m_nodes[itF.value()].x, m_nodes[itF.value()].y,
			                   m_nodes[itT.value()].x, m_nodes[itT.value()].y);
	}

	// Update label item
	if (m_nodes[nodeIdx].label_item)
		m_nodes[nodeIdx].label_item->setPos(pos.x() + 8.0, pos.y() - 6.0);

	markDirty();
}

// =========================================================================
//  Edit mode — add / delete
// =========================================================================

void ccGraphPanelDlg::addNodeAt(const QPointF& scenePos)
{
	bool ok = false;
	const QString label = QInputDialog::getText(
	    this, tr("Add node"), tr("Node label:"), QLineEdit::Normal, QString(), &ok).trimmed();
	if (!ok || label.isEmpty()) return;
	if (m_nodeIndex.contains(label))
	{
		QMessageBox::warning(this, tr("Add node"), tr("Label '%1' already exists.").arg(label));
		return;
	}

	GraphNode node;
	node.label    = label;
	node.nodeType = deriveNodeType(label, QString());
	node.x        = scenePos.x();
	node.y        = scenePos.y();
	node.attrs.insert("label",    node.label);
	node.attrs.insert("NodeType", node.nodeType);
	node.attrs.insert("x", QString::number(node.x, 'f', 3));
	node.attrs.insert("y", QString::number(node.y, 'f', 3));

	const int idx    = m_nodes.size();
	m_nodeIndex.insert(label, idx);
	m_nodes.append(node);

	const double R     = 6.0;
	const bool unlabel = isUnlabelled(label);
	const QColor fill  = nodeColour(node.nodeType);

	auto* item = new NodeItem(idx, this, QRectF(-R, -R, 2 * R, 2 * R));
	item->setPos(node.x, node.y);
	item->setDraggable(true);
	item->setZValue(2.0);
	if (unlabel) { item->setPen(QPen(fill.darker(130), 1.5, Qt::DashLine)); item->setBrush(Qt::NoBrush); }
	else         { item->setPen(QPen(fill.darker(130), 1.0));                item->setBrush(fill); }
	item->setToolTip(label);
	m_scene->addItem(item);
	m_nodes.last().item = item;

	if (!unlabel)
	{
		auto* txt = new QGraphicsSimpleTextItem(label);
		txt->setPos(node.x + R + 2.0, node.y - R);
		txt->setZValue(3.0);
		txt->setBrush(Qt::white);
		QFont f = txt->font(); f.setPointSizeF(5.5); txt->setFont(f);
		m_scene->addItem(txt);
		m_nodes.last().label_item = txt;
	}

	updateStatusLabels();
	markDirty();
}

void ccGraphPanelDlg::deleteNode(int nodeIdx)
{
	if (nodeIdx < 0 || nodeIdx >= m_nodes.size()) return;
	const QString lbl = m_nodes[nodeIdx].label;

	// Remove incident edges first (iterate in reverse so indices stay valid)
	for (int i = m_edges.size() - 1; i >= 0; --i)
		if (m_edges[i].fromLabel == lbl || m_edges[i].toLabel == lbl)
			deleteEdgeInternal(i);

	GraphNode& node = m_nodes[nodeIdx];
	if (node.item)       { m_scene->removeItem(node.item);       delete node.item;       node.item = nullptr; }
	if (node.label_item) { m_scene->removeItem(node.label_item); delete node.label_item; node.label_item = nullptr; }
	m_nodes.remove(nodeIdx);

	// Rebuild index (all indices above nodeIdx shifted by -1)
	m_nodeIndex.clear();
	for (int i = 0; i < m_nodes.size(); ++i)
		m_nodeIndex.insert(m_nodes[i].label, i);

	// NodeItem objects hold stale indices after the remove — safest to rebuild all items
	rebuildSceneItemsOnly();
	updateStatusLabels();
	markDirty();
}

void ccGraphPanelDlg::deleteEdge(int edgeIdx)
{
	deleteEdgeInternal(edgeIdx);
	updateStatusLabels();
	markDirty();
}

void ccGraphPanelDlg::deleteEdgeInternal(int edgeIdx)
{
	if (edgeIdx < 0 || edgeIdx >= m_edges.size()) return;
	GraphEdge& edge = m_edges[edgeIdx];
	if (edge.item) { m_scene->removeItem(edge.item); delete edge.item; edge.item = nullptr; }
	m_edges.remove(edgeIdx);
	// EdgeItem objects hold stale indices — rebuild all items
	rebuildSceneItemsOnly();
}

void ccGraphPanelDlg::changeEdgeType(int edgeIdx)
{
	if (edgeIdx < 0 || edgeIdx >= m_edges.size()) return;
	GraphEdge& edge = m_edges[edgeIdx];
	const QStringList types = {"Path", "Elev", "STAIRS", "Float0", "JPL", "TempEdge"};
	int cur = qMax(0, types.indexOf(edge.edgeType));
	bool ok = false;
	const QString newType = QInputDialog::getItem(
	    this, tr("Edge type"), tr("New type:"), types, cur, false, &ok);
	if (!ok) return;
	edge.edgeType = newType;
	edge.attrs.insert("EdgeType", newType);
	if (edge.item)
	{
		const bool dashed = (newType.toLower() == "float0" || newType.toLower() == "tempedge");
		QPen pen(edgeColour(newType), dashed ? 1.0 : 1.5);
		if (dashed) pen.setStyle(Qt::DashLine);
		edge.item->setPen(pen);
	}
	markDirty();
}

// =========================================================================
//  Slots
// =========================================================================

void ccGraphPanelDlg::onLoadNodes()
{
	const QString path = QFileDialog::getOpenFileName(
	    this, tr("Load Nodes CSV"), QString(), tr("CSV files (*.csv);;All files (*)"));
	if (path.isEmpty()) return;
	if (!loadNodesCsv(path)) return;
	if (!m_edgesCsvPath.isEmpty()) loadEdgesCsv(m_edgesCsvPath);
	rebuildScene();
}

void ccGraphPanelDlg::onLoadEdges()
{
	const QString path = QFileDialog::getOpenFileName(
	    this, tr("Load Edges CSV"), QString(), tr("CSV files (*.csv);;All files (*)"));
	if (path.isEmpty()) return;
	if (!loadEdgesCsv(path)) return;
	rebuildScene();
}

void ccGraphPanelDlg::onRelayout()
{
	if (m_nodes.isEmpty()) return;
	runFruchtermanReingold();
	rebuildSceneItemsOnly();
	m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void ccGraphPanelDlg::onFitView()
{
	if (!m_scene->items().isEmpty())
		m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void ccGraphPanelDlg::onToggleEditMode(bool checked)
{
	m_editMode = checked;
	cancelEdgeDraw();
	// In edit mode use NoDrag so mouse moves nodes rather than the viewport
	m_view->setDragMode(checked ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);

	for (auto& node : m_nodes)
		if (auto* ni = qgraphicsitem_cast<NodeItem*>(node.item))
			ni->setDraggable(checked);

	if (checked)
	{
		editModeButton->setText(tr("View mode"));
		editHintLabel->setText(tr("<i>Left-click empty space: add node<br>"
		                          "Right-click node: edge / delete<br>"
		                          "Right-click edge: type / delete<br>"
		                          "Esc: cancel edge draw</i>"));
	}
	else
	{
		editModeButton->setText(tr("Edit mode"));
		editHintLabel->clear();
	}
}

void ccGraphPanelDlg::onSaveGraph()
{
	if (m_nodesCsvPath.isEmpty() && m_edgesCsvPath.isEmpty())
	{
		QMessageBox::information(this, tr("Save"), tr("No CSV files loaded."));
		return;
	}
	// Sync dragged positions into attrs
	for (auto& node : m_nodes)
	{
		if (node.item) { const QPointF p = node.item->pos(); node.x = p.x(); node.y = p.y(); }
		node.attrs.insert("x", QString::number(node.x, 'f', 3));
		node.attrs.insert("y", QString::number(node.y, 'f', 3));
	}
	if (!m_nodesCsvPath.isEmpty()) saveNodesCsv();
	if (!m_edgesCsvPath.isEmpty()) saveEdgesCsv();
	m_dirty = false;
	saveGraphButton->setEnabled(false);
	statusLabel->setText(tr("Graph saved."));
}

// =========================================================================
//  CSV loading
// =========================================================================

bool ccGraphPanelDlg::loadNodesCsv(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QMessageBox::critical(this, tr("Graph Panel"), tr("Cannot open:\n%1").arg(path));
		return false;
	}
	m_nodesCsvPath = path;
	m_nodes.clear();
	m_nodeIndex.clear();

	QTextStream in(&f);
	QStringList header;
	bool firstLine = true;
	int labelIdx = -1, nodeTypeIdx = -1, xIdx = -1, yIdx = -1;

	while (!in.atEnd())
	{
		const QString line = in.readLine().trimmed();
		if (line.isEmpty()) continue;
		const QStringList cols = line.split(',');

		if (firstLine)
		{
			firstLine = false;
			header    = cols;
			for (auto& h : header) h = h.trimmed();
			labelIdx    = findColIdx(header, "label");
			if (labelIdx < 0) labelIdx = findColIdx(header, "name");
			if (labelIdx < 0) labelIdx = findColIdx(header, "id");
			nodeTypeIdx = findColIdx(header, "NodeType");
			if (nodeTypeIdx < 0) nodeTypeIdx = findColIdx(header, "Type");
			xIdx = findColIdx(header, "x");
			yIdx = findColIdx(header, "y");
			continue;
		}

		if (labelIdx < 0 || labelIdx >= cols.size()) continue;
		GraphNode node;
		node.label = safeCol(cols, labelIdx);
		if (node.label.isEmpty()) continue;
		node.nodeType = deriveNodeType(node.label, safeCol(cols, nodeTypeIdx));
		for (int i = 0; i < header.size() && i < cols.size(); ++i)
			node.attrs.insert(header[i], cols[i].trimmed());
		if (xIdx >= 0) node.x = safeCol(cols, xIdx).toDouble();
		if (yIdx >= 0) node.y = safeCol(cols, yIdx).toDouble();
		m_nodeIndex.insert(node.label, m_nodes.size());
		m_nodes.append(node);
	}

	if (m_nodes.isEmpty())
	{
		QMessageBox::warning(this, tr("Graph Panel"), tr("No nodes found in:\n%1").arg(path));
		return false;
	}
	statusLabel->setText(tr("Nodes: %1").arg(m_nodes.size()));
	return true;
}

bool ccGraphPanelDlg::loadEdgesCsv(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QMessageBox::critical(this, tr("Graph Panel"), tr("Cannot open:\n%1").arg(path));
		return false;
	}
	m_edgesCsvPath = path;
	m_edges.clear();

	QTextStream in(&f);
	QStringList header;
	bool firstLine = true;
	int fromIdx = -1, toIdx = -1, typeIdx = -1;

	while (!in.atEnd())
	{
		const QString line = in.readLine().trimmed();
		if (line.isEmpty()) continue;
		const QStringList cols = line.split(',');

		if (firstLine)
		{
			firstLine = false;
			header    = cols;
			for (auto& h : header) h = h.trimmed();
			fromIdx = findColIdx(header, "FromNode");
			if (fromIdx < 0) fromIdx = findColIdx(header, "From");
			toIdx   = findColIdx(header, "ToNode");
			if (toIdx < 0) toIdx = findColIdx(header, "To");
			typeIdx = findColIdx(header, "EdgeType");
			if (typeIdx < 0) typeIdx = findColIdx(header, "Type");
			continue;
		}

		GraphEdge edge;
		edge.fromLabel = safeCol(cols, fromIdx);
		edge.toLabel   = safeCol(cols, toIdx);
		edge.edgeType  = safeCol(cols, typeIdx);
		if (edge.fromLabel.isEmpty() || edge.toLabel.isEmpty()) continue;
		for (int i = 0; i < header.size() && i < cols.size(); ++i)
			edge.attrs.insert(header[i], cols[i].trimmed());
		m_edges.append(edge);
	}

	statusLabel->setText(tr("Edges: %1").arg(m_edges.size()));
	return true;
}

// =========================================================================
//  Scene building
// =========================================================================

void ccGraphPanelDlg::rebuildScene()
{
	if (m_nodes.isEmpty()) return;

	bool allZero = true;
	for (const auto& n : m_nodes) if (n.x != 0.0 || n.y != 0.0) { allZero = false; break; }
	if (allZero)
		for (auto& n : m_nodes) { n.x = (qrand() % 801) - 400.0; n.y = (qrand() % 801) - 400.0; }

	runFruchtermanReingold();
	m_scene->clear();
	for (auto& n : m_nodes) { n.item = nullptr; n.label_item = nullptr; }
	for (auto& e : m_edges)   e.item = nullptr;

	createEdgeItems();
	createNodeItems();
	updateStatusLabels();

	relayoutButton->setEnabled(true);
	fitButton->setEnabled(true);
	editModeButton->setEnabled(true);

	m_dirty = false;
	saveGraphButton->setEnabled(false);
	m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void ccGraphPanelDlg::rebuildSceneItemsOnly()
{
	m_scene->clear();
	for (auto& n : m_nodes) { n.item = nullptr; n.label_item = nullptr; }
	for (auto& e : m_edges)   e.item = nullptr;
	createEdgeItems();
	createNodeItems();
	if (m_editMode)
		for (auto& node : m_nodes)
			if (auto* ni = qgraphicsitem_cast<NodeItem*>(node.item))
				ni->setDraggable(true);
}

void ccGraphPanelDlg::runFruchtermanReingold()
{
	const int N = m_nodes.size();
	if (N < 2) return;
	const double area = 800.0 * 800.0;
	const double k  = qSqrt(area / N) * 1.5;
	const double k2 = k * k;
	struct Vec2 { double x, y; };
	QVector<Vec2> disp(N, {0.0, 0.0});
	double temp = qSqrt(area) / 2.0;

	for (int iter = 0; iter < 300; ++iter)
	{
		for (auto& d : disp) { d.x = 0.0; d.y = 0.0; }
		for (int i = 0; i < N; ++i)
			for (int j = i + 1; j < N; ++j)
			{
				double dx = m_nodes[i].x - m_nodes[j].x;
				double dy = m_nodes[i].y - m_nodes[j].y;
				double d2 = dx*dx + dy*dy;
				if (d2 < 1e-6) { dx = 0.01; dy = 0.01; d2 = 2e-4; }
				const double d = qSqrt(d2), mag = k2 / d;
				disp[i].x += (dx/d)*mag; disp[i].y += (dy/d)*mag;
				disp[j].x -= (dx/d)*mag; disp[j].y -= (dy/d)*mag;
			}
		for (const auto& edge : m_edges)
		{
			auto itF = m_nodeIndex.find(edge.fromLabel);
			auto itT = m_nodeIndex.find(edge.toLabel);
			if (itF == m_nodeIndex.end() || itT == m_nodeIndex.end()) continue;
			const int i = itF.value(), j = itT.value();
			double dx = m_nodes[i].x - m_nodes[j].x;
			double dy = m_nodes[i].y - m_nodes[j].y;
			double d  = qSqrt(dx*dx + dy*dy); if (d < 1e-6) d = 1e-6;
			const double mag = (d*d) / k;
			disp[i].x -= (dx/d)*mag; disp[i].y -= (dy/d)*mag;
			disp[j].x += (dx/d)*mag; disp[j].y += (dy/d)*mag;
		}
		for (int i = 0; i < N; ++i)
		{
			double len = qSqrt(disp[i].x*disp[i].x + disp[i].y*disp[i].y);
			if (len < 1e-6) continue;
			const double cap = qMin(len, temp);
			m_nodes[i].x += (disp[i].x/len)*cap;
			m_nodes[i].y += (disp[i].y/len)*cap;
		}
		temp *= 0.95;
	}
}

void ccGraphPanelDlg::createNodeItems()
{
	const double R = 6.0;
	for (int idx = 0; idx < m_nodes.size(); ++idx)
	{
		GraphNode& node    = m_nodes[idx];
		const bool unlabel = isUnlabelled(node.label);
		const QColor fill  = nodeColour(node.nodeType);

		auto* item = new NodeItem(idx, this, QRectF(-R, -R, 2*R, 2*R));
		item->setPos(node.x, node.y);
		item->setZValue(2.0);
		item->setDraggable(m_editMode);
		if (unlabel) { item->setPen(QPen(fill.darker(130), 1.5, Qt::DashLine)); item->setBrush(Qt::NoBrush); }
		else         { item->setPen(QPen(fill.darker(130), 1.0));                item->setBrush(fill); }
		item->setToolTip(node.label);
		m_scene->addItem(item);
		node.item = item;

		if (!unlabel)
		{
			auto* txt = new QGraphicsSimpleTextItem(node.label);
			txt->setPos(node.x + R + 2.0, node.y - R);
			txt->setZValue(3.0);
			txt->setBrush(Qt::white);
			QFont f = txt->font(); f.setPointSizeF(5.5); txt->setFont(f);
			m_scene->addItem(txt);
			node.label_item = txt;
		}
	}
}

void ccGraphPanelDlg::createEdgeItems()
{
	for (int idx = 0; idx < m_edges.size(); ++idx)
	{
		GraphEdge& edge = m_edges[idx];
		auto itF = m_nodeIndex.find(edge.fromLabel);
		auto itT = m_nodeIndex.find(edge.toLabel);
		if (itF == m_nodeIndex.end() || itT == m_nodeIndex.end()) continue;

		const GraphNode& nF = m_nodes[itF.value()];
		const GraphNode& nT = m_nodes[itT.value()];
		const bool dashed = (edge.edgeType.toLower() == "float0" ||
		                     edge.edgeType.toLower() == "tempedge" ||
		                     edge.edgeType.toLower() == "temp");
		QPen pen(edgeColour(edge.edgeType), dashed ? 1.0 : 1.5);
		if (dashed) pen.setStyle(Qt::DashLine);

		auto* item = new EdgeItem(idx, this);
		item->setLine(nF.x, nF.y, nT.x, nT.y);
		item->setPen(pen);
		item->setZValue(1.0);
		m_scene->addItem(item);
		edge.item = item;
	}
}

void ccGraphPanelDlg::updateStatusLabels()
{
	nodeCountLabel->setText(tr("Nodes: %1").arg(m_nodes.size()));
	edgeCountLabel->setText(tr("Edges: %1").arg(m_edges.size()));
	int u = 0;
	for (const auto& n : m_nodes) if (isUnlabelled(n.label)) ++u;
	unlabelledCountLabel->setText(u > 0 ? tr("Unlabelled: %1").arg(u) : QString());
}

void ccGraphPanelDlg::markDirty()
{
	if (!m_dirty) { m_dirty = true; saveGraphButton->setEnabled(true); statusLabel->setText(tr("Unsaved changes")); }
}

// =========================================================================
//  CSV write-back
// =========================================================================

void ccGraphPanelDlg::saveNodesCsv() const
{
	QFile f(m_nodesCsvPath);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
	QTextStream out(&f);

	QStringList keys;
	for (const QString& p : {"label", "NodeType", "x", "y", "z"}) keys.append(p);
	for (const auto& node : m_nodes)
		for (const auto& k : node.attrs.keys())
			if (!keys.contains(k)) keys.append(k);

	out << keys.join(',') << "\n";
	for (const auto& node : m_nodes)
	{
		QStringList vals;
		for (const auto& k : keys) vals.append(node.attrs.value(k));
		out << vals.join(',') << "\n";
	}
}

void ccGraphPanelDlg::saveEdgesCsv() const
{
	QFile f(m_edgesCsvPath);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
	QTextStream out(&f);

	QStringList keys;
	for (const QString& p : {"FromNode", "ToNode", "EdgeType"}) keys.append(p);
	for (const auto& edge : m_edges)
		for (const auto& k : edge.attrs.keys())
			if (!keys.contains(k)) keys.append(k);

	out << keys.join(',') << "\n";
	for (const auto& edge : m_edges)
	{
		QStringList vals;
		for (const auto& k : keys) vals.append(edge.attrs.value(k));
		out << vals.join(',') << "\n";
	}
}

// =========================================================================
//  Colour helpers
// =========================================================================

QColor ccGraphPanelDlg::nodeColour(const QString& nodeType)
{
	const QString t = nodeType.toLower();
	if (t == "platformexit")                     return QColor(Qt::black);
	if (t == "con"  || t == "base")              return QColor(160, 160, 160);
	if (t == "top")                              return QColor(160, 32, 240);
	if (t == "trainfront" || t == "trainffront") return QColor(Qt::green);
	if (t == "trainrear")                        return QColor(139, 69, 19);
	if (t == "jpl" || t == "journeypatternlink") return QColor(255, 165, 0);
	if (t == "exit" || t == "streetexit")        return QColor(220, 200, 50);
	if (t == "bookinghall")                      return QColor(200, 200, 50);
	return QColor(100, 160, 220);
}

QColor ccGraphPanelDlg::edgeColour(const QString& edgeType)
{
	const QString t = edgeType.toLower();
	if (t == "path")                                                     return QColor(0, 200, 200);
	if (t == "elev" || t == "stairs" || t == "escalator" || t == "lift") return QColor(Qt::red);
	if (t == "float0")                                                    return QColor(120, 120, 120);
	if (t == "jpl")                                                       return QColor(60, 100, 220);
	if (t == "tempedge" || t == "temp")                                   return QColor(Qt::red);
	return QColor(180, 180, 180);
}

bool ccGraphPanelDlg::isUnlabelled(const QString& label)
{
	return label.isEmpty() || label.startsWith("Point #");
}

QString ccGraphPanelDlg::deriveNodeType(const QString& label, const QString& csvNodeType)
{
	if (!csvNodeType.isEmpty()) return csvNodeType;
	const QString last = label.split('.').last().trimmed();
	if (last.compare("Con",         Qt::CaseInsensitive) == 0) return "Con";
	if (last.compare("Base",        Qt::CaseInsensitive) == 0) return "Base";
	if (last.compare("Top",         Qt::CaseInsensitive) == 0) return "Top";
	if (last.compare("F",           Qt::CaseInsensitive) == 0) return "TrainFront";
	if (last.compare("R",           Qt::CaseInsensitive) == 0) return "TrainRear";
	if (last.compare("Exit",        Qt::CaseInsensitive) == 0) return "Exit";
	if (last.compare("StreetExit",  Qt::CaseInsensitive) == 0) return "StreetExit";
	if (last.compare("BookingHall", Qt::CaseInsensitive) == 0) return "BookingHall";
	if (last.compare("JPL",         Qt::CaseInsensitive) == 0) return "JPL";
	static const QRegularExpression reDigit("^\\d+$");
	if (reDigit.match(last).hasMatch()) return "JourneyPatternLink";
	static const QRegularExpression rePlatform("^\\d+[A-Za-z]+$");
	if (rePlatform.match(last).hasMatch()) return "PlatformExit";
	return QString();
}
