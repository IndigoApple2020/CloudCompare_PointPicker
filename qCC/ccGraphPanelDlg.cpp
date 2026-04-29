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
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtMath>

// =========================================================================
//  Local QGraphicsView with wheel-zoom and left-drag pan
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
		setBackgroundBrush(QColor(30, 30, 30)); // dark canvas
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
//  Clickable node item — shows inspector on click
// =========================================================================

class NodeItem : public QGraphicsEllipseItem
{
  public:
	NodeItem(int nodeIdx, ccGraphPanelDlg* dlg, const QRectF& rect, QGraphicsItem* parent = nullptr)
	    : QGraphicsEllipseItem(rect, parent), m_nodeIdx(nodeIdx), m_dlg(dlg)
	{
		setFlag(QGraphicsItem::ItemIsSelectable, true);
		setAcceptHoverEvents(true);
	}

  protected:
	void mousePressEvent(QGraphicsSceneMouseEvent* event) override
	{
		QGraphicsEllipseItem::mousePressEvent(event);
		if (event->button() == Qt::LeftButton)
			m_dlg->showNodeInspector(m_nodeIdx);
	}

	void hoverEnterEvent(QGraphicsSceneHoverEvent* /*event*/) override
	{
		setOpacity(0.75);
	}
	void hoverLeaveEvent(QGraphicsSceneHoverEvent* /*event*/) override
	{
		setOpacity(1.0);
	}

  private:
	int              m_nodeIdx;
	ccGraphPanelDlg* m_dlg;
};

// =========================================================================
//  Clickable edge item
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
//  ccGraphPanelDlg
// =========================================================================

ccGraphPanelDlg::ccGraphPanelDlg(QWidget* parent)
    : QDialog(parent, Qt::Window)
    , Ui::GraphPanelDlg()
{
	setupUi(this);
	setWindowTitle(tr("Graph Panel — BatGraph F8"));

	// Embed GraphView into the graphContainer placeholder
	m_view  = new GraphView(graphContainer);
	m_scene = new QGraphicsScene(this);
	m_view->setScene(m_scene);

	auto* lay = new QVBoxLayout(graphContainer);
	lay->setContentsMargins(0, 0, 0, 0);
	lay->addWidget(m_view);

	// Connect toolbar buttons
	connect(loadNodesButton, &QPushButton::clicked, this, &ccGraphPanelDlg::onLoadNodes);
	connect(loadEdgesButton, &QPushButton::clicked, this, &ccGraphPanelDlg::onLoadEdges);
	connect(relayoutButton,  &QPushButton::clicked, this, &ccGraphPanelDlg::onRelayout);
	connect(fitButton,       &QPushButton::clicked, this, &ccGraphPanelDlg::onFitView);
	connect(closeButton,     &QPushButton::clicked, this, &QDialog::accept);
}

// -------------------------------------------------------------------------
//  Public
// -------------------------------------------------------------------------

void ccGraphPanelDlg::reload()
{
	if (!m_nodesCsvPath.isEmpty())
		loadNodesCsv(m_nodesCsvPath);
	if (!m_edgesCsvPath.isEmpty())
		loadEdgesCsv(m_edgesCsvPath);
	rebuildScene();
}

// -------------------------------------------------------------------------
//  Slots
// -------------------------------------------------------------------------

void ccGraphPanelDlg::onLoadNodes()
{
	const QString path = QFileDialog::getOpenFileName(
	    this, tr("Load Nodes CSV"), QString(), tr("CSV files (*.csv);;All files (*)"));
	if (path.isEmpty())
		return;

	if (!loadNodesCsv(path))
		return;

	// If edges already loaded, rebuild; otherwise just show node count
	if (!m_edgesCsvPath.isEmpty())
		loadEdgesCsv(m_edgesCsvPath); // reload with new node set
	rebuildScene();
}

void ccGraphPanelDlg::onLoadEdges()
{
	const QString path = QFileDialog::getOpenFileName(
	    this, tr("Load Edges CSV"), QString(), tr("CSV files (*.csv);;All files (*)"));
	if (path.isEmpty())
		return;

	if (!loadEdgesCsv(path))
		return;

	rebuildScene();
}

void ccGraphPanelDlg::onRelayout()
{
	if (m_nodes.isEmpty())
		return;
	runFruchtermanReingold();
	createEdgeItems(); // update edge positions
	createNodeItems(); // recreate node items at new positions
	m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void ccGraphPanelDlg::onFitView()
{
	if (!m_scene->items().isEmpty())
		m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

// -------------------------------------------------------------------------
//  CSV loading
// -------------------------------------------------------------------------

bool ccGraphPanelDlg::loadNodesCsv(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QMessageBox::critical(this, tr("Graph Panel"), tr("Cannot open nodes file:\n%1").arg(path));
		return false;
	}

	m_nodesCsvPath = path;
	m_nodes.clear();
	m_nodeIndex.clear();

	QTextStream in(&f);
	QStringList header;
	bool        firstLine = true;

	// Column indices
	int labelIdx    = -1;
	int nodeTypeIdx = -1;
	int xIdx = -1, yIdx = -1, zIdx = -1;

	while (!in.atEnd())
	{
		const QString line = in.readLine().trimmed();
		if (line.isEmpty())
			continue;

		const QStringList cols = line.split(',');

		if (firstLine)
		{
			firstLine = false;
			header    = cols;
			for (auto& h : header)
				h = h.trimmed();

			// Resolve important columns (flexible naming)
			labelIdx    = findColIdx(header, "label");
			if (labelIdx < 0) labelIdx = findColIdx(header, "name");
			if (labelIdx < 0) labelIdx = findColIdx(header, "id");
			nodeTypeIdx = findColIdx(header, "NodeType");
			if (nodeTypeIdx < 0) nodeTypeIdx = findColIdx(header, "Type");
			xIdx = findColIdx(header, "x");
			yIdx = findColIdx(header, "y");
			zIdx = findColIdx(header, "z");
			continue;
		}

		if (labelIdx < 0 || labelIdx >= cols.size())
			continue; // skip rows without a label

		GraphNode node;
		node.label = safeCol(cols, labelIdx);
		if (node.label.isEmpty())
			continue;

		const QString csvNodeType = safeCol(cols, nodeTypeIdx);
		node.nodeType = deriveNodeType(node.label, csvNodeType);

		// Store all attributes
		for (int i = 0; i < header.size() && i < cols.size(); ++i)
			node.attrs.insert(header[i], cols[i].trimmed());

		// Use 3-D coordinates as initial positions if present (projected to x/y)
		if (xIdx >= 0) node.x = safeCol(cols, xIdx).toDouble();
		if (yIdx >= 0) node.y = safeCol(cols, yIdx).toDouble();
		// z ignored for 2-D layout — F-R will scatter nodes anyway

		const int idx = m_nodes.size();
		m_nodeIndex.insert(node.label, idx);
		m_nodes.append(node);
	}

	if (m_nodes.isEmpty())
	{
		QMessageBox::warning(this, tr("Graph Panel"), tr("No nodes found in:\n%1").arg(path));
		return false;
	}

	statusLabel->setText(tr("Nodes loaded: %1").arg(m_nodes.size()));
	return true;
}

bool ccGraphPanelDlg::loadEdgesCsv(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QMessageBox::critical(this, tr("Graph Panel"), tr("Cannot open edges file:\n%1").arg(path));
		return false;
	}

	m_edgesCsvPath = path;
	m_edges.clear();

	QTextStream in(&f);
	QStringList header;
	bool        firstLine = true;

	int fromIdx = -1, toIdx = -1, typeIdx = -1;

	while (!in.atEnd())
	{
		const QString line = in.readLine().trimmed();
		if (line.isEmpty())
			continue;

		const QStringList cols = line.split(',');

		if (firstLine)
		{
			firstLine = false;
			header    = cols;
			for (auto& h : header)
				h = h.trimmed();

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

		if (edge.fromLabel.isEmpty() || edge.toLabel.isEmpty())
			continue;

		for (int i = 0; i < header.size() && i < cols.size(); ++i)
			edge.attrs.insert(header[i], cols[i].trimmed());

		m_edges.append(edge);
	}

	statusLabel->setText(tr("Edges loaded: %1").arg(m_edges.size()));
	return true;
}

// -------------------------------------------------------------------------
//  Scene building
// -------------------------------------------------------------------------

void ccGraphPanelDlg::rebuildScene()
{
	if (m_nodes.isEmpty())
		return;

	// Scatter nodes randomly in virtual space if they all sit at 0,0
	{
		bool allZero = true;
		for (const auto& n : m_nodes)
			if (n.x != 0.0 || n.y != 0.0) { allZero = false; break; }
		if (allZero)
		{
			// Random scatter in [-400, 400]
			for (auto& n : m_nodes)
			{
				n.x = (qrand() % 801) - 400.0;
				n.y = (qrand() % 801) - 400.0;
			}
		}
	}

	runFruchtermanReingold();
	m_scene->clear();
	// After clear, all item pointers in nodes/edges are dangling — reset them
	for (auto& n : m_nodes) { n.item = nullptr; n.label_item = nullptr; }
	for (auto& e : m_edges) e.item = nullptr;

	createEdgeItems();
	createNodeItems();
	updateStatusLabels();

	relayoutButton->setEnabled(true);
	fitButton->setEnabled(true);

	m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

// -------------------------------------------------------------------------
//  Fruchterman-Reingold layout
// -------------------------------------------------------------------------

void ccGraphPanelDlg::runFruchtermanReingold()
{
	const int    N    = m_nodes.size();
	if (N < 2)
		return;

	const double area = 800.0 * 800.0;
	const double k    = qSqrt(area / N) * 1.5;
	const double k2   = k * k;

	// Build a fast adjacency lookup: node-index → neighbour indices
	// (not strictly needed for F-R but used to limit repulsion radius)

	double temp = qSqrt(area) / 2.0;

	struct Vec2 { double x, y; };
	QVector<Vec2> disp(N, {0.0, 0.0});

	for (int iter = 0; iter < 300; ++iter)
	{
		// Reset displacements
		for (auto& d : disp) { d.x = 0.0; d.y = 0.0; }

		// --- Repulsive forces (all pairs) ---
		for (int i = 0; i < N; ++i)
		{
			for (int j = i + 1; j < N; ++j)
			{
				double dx = m_nodes[i].x - m_nodes[j].x;
				double dy = m_nodes[i].y - m_nodes[j].y;
				double d2 = dx * dx + dy * dy;
				if (d2 < 1e-6) { dx = 0.01; dy = 0.01; d2 = 2e-4; }
				const double d    = qSqrt(d2);
				const double mag  = k2 / d;
				disp[i].x += (dx / d) * mag;
				disp[i].y += (dy / d) * mag;
				disp[j].x -= (dx / d) * mag;
				disp[j].y -= (dy / d) * mag;
			}
		}

		// --- Attractive forces (edges) ---
		for (const auto& edge : m_edges)
		{
			auto itF = m_nodeIndex.find(edge.fromLabel);
			auto itT = m_nodeIndex.find(edge.toLabel);
			if (itF == m_nodeIndex.end() || itT == m_nodeIndex.end())
				continue;
			const int i = itF.value();
			const int j = itT.value();

			double dx = m_nodes[i].x - m_nodes[j].x;
			double dy = m_nodes[i].y - m_nodes[j].y;
			double d  = qSqrt(dx * dx + dy * dy);
			if (d < 1e-6) d = 1e-6;
			const double mag = (d * d) / k;
			disp[i].x -= (dx / d) * mag;
			disp[i].y -= (dy / d) * mag;
			disp[j].x += (dx / d) * mag;
			disp[j].y += (dy / d) * mag;
		}

		// --- Apply displacements capped to temperature ---
		for (int i = 0; i < N; ++i)
		{
			double dLen = qSqrt(disp[i].x * disp[i].x + disp[i].y * disp[i].y);
			if (dLen < 1e-6) continue;
			const double cap = qMin(dLen, temp);
			m_nodes[i].x += (disp[i].x / dLen) * cap;
			m_nodes[i].y += (disp[i].y / dLen) * cap;
		}

		temp *= 0.95;
	}
}

// -------------------------------------------------------------------------
//  Scene item creation
// -------------------------------------------------------------------------

void ccGraphPanelDlg::createNodeItems()
{
	const double R = 6.0; // node radius

	for (int idx = 0; idx < m_nodes.size(); ++idx)
	{
		GraphNode& node    = m_nodes[idx];
		const bool unlabel = isUnlabelled(node.label);

		QColor fill  = nodeColour(node.nodeType);
		QColor pen   = fill.darker(130);

		auto* item = new NodeItem(idx, this, QRectF(-R, -R, 2 * R, 2 * R));
		item->setPos(node.x, node.y);
		item->setZValue(2.0);

		if (unlabel)
		{
			// Hollow: no fill, dashed outline
			QPen p(pen, 1.5, Qt::DashLine);
			item->setPen(p);
			item->setBrush(Qt::NoBrush);
		}
		else
		{
			item->setPen(QPen(pen, 1.0));
			item->setBrush(fill);
		}

		item->setToolTip(node.label);
		m_scene->addItem(item);
		node.item = item;

		// Label text — only for labelled nodes to keep the view readable
		if (!unlabel)
		{
			auto* txt = new QGraphicsSimpleTextItem(node.label);
			txt->setPos(node.x + R + 2.0, node.y - R);
			txt->setZValue(3.0);
			txt->setBrush(Qt::white);
			// Small font
			QFont f = txt->font();
			f.setPointSizeF(5.5);
			txt->setFont(f);
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
		if (itF == m_nodeIndex.end() || itT == m_nodeIndex.end())
			continue;

		const GraphNode& nF = m_nodes[itF.value()];
		const GraphNode& nT = m_nodes[itT.value()];

		QColor col = edgeColour(edge.edgeType);

		// Check for Float0 (grey dashed) or TempEdge (red dashed)
		const QString et = edge.edgeType.toLower();
		bool dashed = (et == "float0" || et == "tempedge" || et == "temp");

		QPen pen(col, dashed ? 1.0 : 1.5);
		if (dashed)
			pen.setStyle(Qt::DashLine);

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

	int unlabCount = 0;
	for (const auto& n : m_nodes)
		if (isUnlabelled(n.label))
			++unlabCount;

	if (unlabCount > 0)
		unlabelledCountLabel->setText(tr("Unlabelled: %1").arg(unlabCount));
	else
		unlabelledCountLabel->clear();
}

// -------------------------------------------------------------------------
//  Inspector (called by NodeItem / EdgeItem)
// -------------------------------------------------------------------------

void ccGraphPanelDlg::showNodeInspector(int nodeIdx)
{
	if (nodeIdx < 0 || nodeIdx >= m_nodes.size())
		return;

	const GraphNode& node = m_nodes[nodeIdx];
	inspectorTitleLabel->setText(tr("<b>Node:</b> %1").arg(node.label.toHtmlEscaped()));

	QString text;
	for (auto it = node.attrs.cbegin(); it != node.attrs.cend(); ++it)
		text += it.key() + " = " + it.value() + "\n";
	inspectorText->setPlainText(text.trimmed());
}

void ccGraphPanelDlg::showEdgeInspector(int edgeIdx)
{
	if (edgeIdx < 0 || edgeIdx >= m_edges.size())
		return;

	const GraphEdge& edge = m_edges[edgeIdx];
	inspectorTitleLabel->setText(
	    tr("<b>Edge:</b> %1 → %2").arg(edge.fromLabel.toHtmlEscaped(), edge.toLabel.toHtmlEscaped()));

	QString text;
	for (auto it = edge.attrs.cbegin(); it != edge.attrs.cend(); ++it)
		text += it.key() + " = " + it.value() + "\n";
	inspectorText->setPlainText(text.trimmed());
}

// -------------------------------------------------------------------------
//  Colour helpers
// -------------------------------------------------------------------------

QColor ccGraphPanelDlg::nodeColour(const QString& nodeType)
{
	const QString t = nodeType.toLower();
	if (t == "platformexit")    return QColor(Qt::black);
	if (t == "con" || t == "base") return QColor(160, 160, 160);
	if (t == "top")             return QColor(160, 32, 240);  // purple
	if (t == "trainffront" || t == "trainfront") return QColor(Qt::green);
	if (t == "trainrear")       return QColor(139, 69, 19);   // brown
	if (t == "jpl" || t == "journeypatternlink") return QColor(255, 165, 0); // orange
	if (t == "exit" || t == "streetexit")        return QColor(220, 200, 50); // yellow
	if (t == "bookinghall")     return QColor(200, 200, 50);
	// Default: mid-blue
	return QColor(100, 160, 220);
}

QColor ccGraphPanelDlg::edgeColour(const QString& edgeType)
{
	const QString t = edgeType.toLower();
	if (t == "path")            return QColor(0, 200, 200);   // cyan
	if (t == "elev" || t == "stairs" || t == "escalator" || t == "lift")
		return QColor(Qt::red);
	if (t == "float0")          return QColor(120, 120, 120); // grey (dashed)
	if (t == "jpl")             return QColor(60, 100, 220);  // blue
	if (t == "tempedge" || t == "temp") return QColor(Qt::red); // red (dashed)
	return QColor(180, 180, 180); // default: light grey
}

bool ccGraphPanelDlg::isUnlabelled(const QString& label)
{
	return label.isEmpty() || label.startsWith("Point #");
}

QString ccGraphPanelDlg::deriveNodeType(const QString& label, const QString& csvNodeType)
{
	// Prefer explicit CSV column value
	if (!csvNodeType.isEmpty())
		return csvNodeType;

	// Derive from last segment of dot-split name
	// e.g. "LU.PAC.Eli.W.5A" → "5A" → PlatformExit
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

	// Digit-only → JourneyPatternLink
	static const QRegularExpression reDigit("^\\d+$");
	if (reDigit.match(last).hasMatch())
		return "JourneyPatternLink";

	// Digit + letter suffix (e.g. 5A, 12B) → PlatformExit
	static const QRegularExpression rePlatform("^\\d+[A-Za-z]+$");
	if (rePlatform.match(last).hasMatch())
		return "PlatformExit";

	return QString(); // unknown
}
