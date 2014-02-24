#include "QGraphicsMapScene.h"
#include <QGraphicsBlurEffect>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QScrollBar>
#include <iomanip>
#include <sstream>
#include "../core.h"
#include <lmu_reader.h>

QGraphicsMapScene::QGraphicsMapScene(int id, QGraphicsView *view, QObject *parent) :
    QGraphicsScene(parent)
{
    m_view = view;
    m_view->setMouseTracking(true);
    std::stringstream ss;
    ss << mCore()->filePath(ROOT).toStdString()
       << "Map"
       << std::setfill('0')
       << std::setw(4)
       << id
       << ".emu";
    m_map = LMU_Reader::LoadXml(ss.str());
    m_map.get()->ID = id;
    m_lower =  m_map.get()->lower_layer;
    m_upper =  m_map.get()->upper_layer;
    m_lowerpix = new QGraphicsPixmapItem();
    m_upperpix = new QGraphicsPixmapItem();
    if(m_map.get()->parallax_flag)
        mCore()->LoadBackground(m_map.get()->parallax_name.c_str());
    else
        mCore()->LoadBackground(QString());
    addItem(m_lowerpix);
    addItem(m_upperpix);
    m_drawing = false;
    m_cancelled = false;
    m_selecting = false;
    QGraphicsBlurEffect * effect = new QGraphicsBlurEffect(this);
    effect->setBlurRadius(2.0);
    effect->setBlurHints(QGraphicsBlurEffect::PerformanceHint);
    m_lowerpix->setGraphicsEffect(effect);
    m_upperpix->setGraphicsEffect(new QGraphicsOpacityEffect(this));
    for (int x = 0; x <= m_map.get()->width; x++)
    {
        QGraphicsLineItem* line = new QGraphicsLineItem(x*mCore()->tileSize(),
                                                        0,
                                                        x*mCore()->tileSize(),
                                                        m_map.get()->height*mCore()->tileSize());
        m_lines.append(line);
        addItem(line);
    }
    for (int y = 0; y <= m_map.get()->height; y++)
    {
        QGraphicsLineItem* line = new QGraphicsLineItem(0,
                                                        y*mCore()->tileSize(),
                                                        m_map.get()->width*mCore()->tileSize(),
                                                        y*mCore()->tileSize());
        m_lines.append(line);
        addItem(line);
    }
    onLayerChanged();
    onToolChanged();
}

QGraphicsMapScene::~QGraphicsMapScene()
{
    delete m_lowerpix;
    delete m_upperpix;
    m_eventpixs.clear();

}

float QGraphicsMapScene::scale() const
{
    return m_scale;
}

int QGraphicsMapScene::id() const
{
    return m_map.get()->ID;
}

int QGraphicsMapScene::chipsetId() const
{
    return m_map.get()->chipset_id;
}

void QGraphicsMapScene::redrawMap()
{
    mCore()->LoadChipset(m_map.get()->chipset_id);
    s_tileSize = mCore()->tileSize()*m_scale;
    redrawLayer(Core::LOWER);
    redrawLayer(Core::UPPER);
}

void QGraphicsMapScene::setScale(float scale)
{
    m_scale = scale;
    for (int i = 0; i < m_lines.count(); i++)
        m_lines[i]->setScale(m_scale);
    this->setSceneRect(0,
                       0,
                       m_map.get()->width* mCore()->tileSize()*m_scale,
                       m_map.get()->height* mCore()->tileSize()*m_scale);
    redrawMap();
}

void QGraphicsMapScene::onLayerChanged()
{
    switch (mCore()->layer())
    {
    case (Core::LOWER):
        m_lowerpix->graphicsEffect()->setEnabled(false);
        m_upperpix->graphicsEffect()->setEnabled(true);
        break;
    case (Core::UPPER):
        m_lowerpix->graphicsEffect()->setEnabled(true);
        m_upperpix->graphicsEffect()->setEnabled(false);
        break;
    default:
        m_lowerpix->graphicsEffect()->setEnabled(false);
        m_upperpix->graphicsEffect()->setEnabled(false);
        break;
    }
    for (int i = 0; i < m_lines.count(); i++)
        m_lines[i]->setVisible(mCore()->layer() == Core::EVENT);
}

void QGraphicsMapScene::onToolChanged()
{
    switch (mCore()->tool())
    {
    case (Core::SELECTION):
        m_view->setCursor(QCursor(Qt::ArrowCursor));
        break;
    case (Core::ZOOM):
        m_view->setCursor(QCursor(Qt::UpArrowCursor));
        break;
    case (Core::PENCIL):
        m_view->setCursor(QCursor(Qt::CrossCursor));
        break;
    case (Core::RECTANGLE):
        m_view->setCursor(QCursor(Qt::WaitCursor));
        break;
    case (Core::CIRCLE):
        m_view->setCursor(QCursor(Qt::IBeamCursor));
        break;
    case (Core::FILL):
        m_view->setCursor(QCursor(Qt::PointingHandCursor));
        break;
    }
}

void QGraphicsMapScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_drawing && event->button() == Qt::RightButton)
    {
        stopDrawing();
        return;
    }
    if (m_selecting && event->button() == Qt::LeftButton)
    {
        stopSelecting();
        return;
    }
    if(event->button() == Qt::LeftButton) //Start drawing
    {
        fst_x = cur_x;
        fst_y = cur_y;
        switch(mCore()->tool())
        {
        case (Core::PENCIL):
            m_drawing = true;
            drawPen();
        }
    }
    if(event->button() == Qt::RightButton) //StartSelecting
    {

    }
}

void QGraphicsMapScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (cur_x == event->scenePos().x()/s_tileSize && cur_y == event->scenePos().y()/s_tileSize)
        return;
    cur_x = event->scenePos().x()/s_tileSize;
    cur_y = event->scenePos().y()/s_tileSize;
    if (m_drawing)
    {
        switch (mCore()->tool())
        {
        case (Core::PENCIL):
            drawPen();
        }
    }
}

void QGraphicsMapScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)
    if (m_cancelled)
    {
        m_drawing = false;
        return;
    }
    if (m_drawing && !m_cancelled)
    {
        m_drawing = false;
        if (mCore()->layer() == Core::LOWER)
        {
            // Add lower_layer to undo stack
            m_map.get()->lower_layer = m_lower;
        }
        else
        {
            // Add upper_layer to undo stack
            m_map.get()->upper_layer = m_upper;
        }
    }
}

void QGraphicsMapScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event)
    if (mCore()->layer() != Core::EVENT)
        return;
    //Open event edit dialog
}

int QGraphicsMapScene::_x(int index)
{
    return (index%m_map.get()->width);
}

int QGraphicsMapScene::_y(int index)
{
    return (index/m_map.get()->width);
}

int QGraphicsMapScene::_index(int x, int y)
{
    return (m_map.get()->width*y+x);
}

void QGraphicsMapScene::redrawTile(const Core::Layer &layer,
                                   const int &x,
                                   const int &y,
                                   const QRect &dest_rec)
{
    switch (layer)
    {
    case (Core::LOWER):
        mCore()->renderTile(m_lower[_index(x,y)],dest_rec);
        break;
    case (Core::UPPER):
        mCore()->renderTile(m_upper[_index(x,y)],dest_rec);
        break;
    default:
        break;
    }
}

void QGraphicsMapScene::stopDrawing()
{
    m_cancelled = true;
    m_drawing = false;
    switch (mCore()->layer())
    {
    case (Core::LOWER):
        m_lower = m_map.get()->lower_layer;
        break;
    case (Core::UPPER):
        m_upper = m_map.get()->upper_layer;
        break;
    default:
        break;
    }
    redrawLayer(mCore()->layer());
}

void QGraphicsMapScene::stopSelecting()
{
    m_cancelled = true;
    m_selecting = false;
    //cancel selection...
}

void QGraphicsMapScene::updateArea(int x1, int y1, int x2, int y2)
{
    //Normalize
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 >= m_map.get()->width)
        x1 = m_map.get()->width - 1;
    if (y2 >= m_map.get()->height)
        y2 = m_map.get()->height - 1;

    for (int x = x1; x <= x2; x++)
        for (int y = y1; y <= y2; y++)
        {
            if (mCore()->layer() == Core::LOWER)
            {
                if (!mCore()->isEblock(mCore()->translate(m_lower[_index(x, y)])) &&
                    !mCore()->isAnimation(mCore()->translate(m_lower[_index(x, y)])))
                    m_lower[_index(x,y)] = bind(x, y);
            }

        }
    redrawLayer(mCore()->layer());
}

void QGraphicsMapScene::redrawLayer(Core::Layer layer)
{
    QSize size = m_view->size();
    if (size.width() > m_map.get()->width*s_tileSize)
        size.setWidth(m_map.get()->width*s_tileSize);
    else
        size.setWidth(size.width()+s_tileSize);
    if (size.height() > m_map.get()->height*s_tileSize)
        size.setHeight(m_map.get()->height*s_tileSize);
    else
        size.setHeight(size.height()+s_tileSize);
    int start_x = 0;
    if (m_view->horizontalScrollBar()->isVisible())
        start_x =m_view->horizontalScrollBar()->value()/s_tileSize;
    int start_y = 0;
    if (m_view->verticalScrollBar()->isVisible())
        start_y = m_view->verticalScrollBar()->value()/s_tileSize;
    int end_x = start_x+(size.width()-1)/s_tileSize;
    int end_y = start_y+(size.height()-1)/s_tileSize;
    QPixmap pix(size);
    pix.fill(QColor(0,0,0,0));
    mCore()->beginPainting(pix);
    for (int x = start_x; x <= end_x; x++)
        for (int y = start_y; y <= end_y; y++)
        {
            if (x >= m_map.get()->width || y >= m_map.get()->height)
                continue;
            QRect dest_rect((x-start_x)* s_tileSize,
                       (y-start_y)* s_tileSize,
                       s_tileSize,
                       s_tileSize);
            redrawTile(layer, x, y, dest_rect);
        }
    if (layer == Core::UPPER)
    {
        for (unsigned int i = 0; i <  m_map.get()->events.size(); i++)
        {
            QRect rect((m_map.get()->events[i].x-start_x)* s_tileSize,
                       (m_map.get()->events[i].y-start_y)* s_tileSize,
                       s_tileSize,
                       s_tileSize);
            mCore()->renderTile(EV, rect);
        }
    }
    mCore()->endPainting();
    if (layer == Core::LOWER)
        m_lowerpix->setPixmap(pix);
    else
        m_upperpix->setPixmap(pix);
}

void QGraphicsMapScene::drawPen()
{
    for (int x = cur_x; x < cur_x + mCore()->selWidth(); x++)
        for (int y = cur_y; y < cur_y + mCore()->selHeight(); y++)
        {
            if (mCore()->layer() == Core::LOWER)
                m_lower[_index(x,y)] = mCore()->selection(x-fst_x,y-fst_y);
            else if (mCore()->layer() == Core::UPPER)
                m_upper[_index(x,y)] = mCore()->selection(x-fst_x,y-fst_y);
        }
    updateArea(cur_x-1,cur_y-1,cur_x+mCore()->selWidth()+1,cur_y+mCore()->selHeight()+1);
}

short QGraphicsMapScene::bind(int x, int y)
{
#define tile_u mCore()->translate(m_lower[_index(x, y-1)])
#define tile_d mCore()->translate(m_lower[_index(x, y+1)])
#define tile_l mCore()->translate(m_lower[_index(x-1, y)])
#define tile_r mCore()->translate(m_lower[_index(x+1, y)])
#define tile_ul mCore()->translate(m_lower[_index(x-1, y-1)])
#define tile_ur mCore()->translate(m_lower[_index(x+1, y-1)])
#define tile_dl mCore()->translate(m_lower[_index(x-1, y+1)])
#define tile_dr mCore()->translate(m_lower[_index(x+1, y+1)])
    int _code = 0, _scode = 0;
    int terrain_id = mCore()->translate(m_lower[_index(x, y)]);
    int u=0,d=0,l=0,r=0,ul=0,ur=0,dl=0,dr=0,sul=0,sur=0,sdl=0,sdr=0;
    if (mCore()->isDblock(terrain_id))
    {
        if (y > 0 && terrain_id != tile_u)
            u = UP;
        if (y < m_map.get()->height-1 && terrain_id != tile_d)
            d = DOWN;
        if (x > 0 && terrain_id != tile_l)
            l = LEFT;
        if (x < m_map.get()->width-1 && terrain_id != tile_r)
            r = RIGHT;
        if (u+l == 0 && x > 0 && y > 0 && terrain_id != tile_ul)
            ul = UPLEFT;
        if (u+r == 0 && x < m_map.get()->width-1 && y > 0 && terrain_id != tile_ur)
            ur = UPRIGHT;
        if (d+l == 0 && x > 0 && y < m_map.get()->height-1 && terrain_id != tile_dl)
            dl = DOWNLEFT;
        if (d+r == 0 && x < m_map.get()->width-1 &&
                y < m_map.get()->height-1 && terrain_id != tile_dr)
            dr = DOWNRIGHT;
        _code = u+d+l+r+ul+ur+dl+dr;
    }
    else if (mCore()->isWater(terrain_id))
    {
        if (y > 0 && (!mCore()->isWater(tile_u) &&
                      !mCore()->isAnimation(tile_u)))
            u = UP;
        if (y < m_map.get()->height-1 && (!mCore()->isWater(tile_d) &&
                                          !mCore()->isAnimation(tile_d)))
            d = DOWN;
        if (x > 0 && (!mCore()->isWater(tile_l) &&
                      !mCore()->isAnimation(tile_l)))
            l = LEFT;
        if (x < m_map.get()->width-1 && (!mCore()->isWater(tile_r) &&
                                         !mCore()->isAnimation(tile_r)))
            r = RIGHT;
        if ((u+l) == 0 && !mCore()->isWater(tile_ul))
            ul = UPLEFT;
        if ((u+r) == 0 && !mCore()->isWater(tile_ur))
            ur = UPRIGHT;
        if ((d+l) == 0 && !mCore()->isWater(tile_dl))
            dl = DOWNLEFT;
        if ((d+r) == 0 && !mCore()->isWater(tile_dr))
            dr = DOWNRIGHT;
        _code = u+d+l+r+ul+ur+dl+dr;
        // DeepWater Special Corners
        if (mCore()->isDWater(terrain_id))
        {
            if (x > 0 && y > 0 && mCore()->isABWater(tile_u) && mCore()->isABWater (tile_l) && mCore()->isABWater (tile_ul))
                sul = UPLEFT;
            if (x < m_map.get()->width-1 && y > 0 && mCore()->isABWater(tile_u) && mCore()->isABWater (tile_r) && mCore()->isABWater (tile_ur))
                sur = UPRIGHT;
            if (x > 0 && y < m_map.get()->height-1 && mCore()->isABWater(tile_d) && mCore()->isABWater (tile_l) && mCore()->isABWater (tile_dl))
                sdl = DOWNLEFT;
            if (x < m_map.get()->width-1 && y < m_map.get()->height-1 &&
                    mCore()->isABWater(tile_d) && mCore()->isABWater (tile_r) && mCore()->isABWater (tile_dr))
                sdr = DOWNRIGHT;
        }
        else
        {
            if (x > 0 && y > 0 && mCore()->isDWater (tile_u) && mCore()->isDWater (tile_l) && mCore()->isWater(tile_ul))
                sul = UPLEFT;
            if (x < m_map.get()->width-1 && y > 0 && mCore()->isDWater (tile_u) && mCore()->isDWater (tile_r) && mCore()->isWater(tile_ur))
                sur = UPRIGHT;
            if (x > 0 && y < m_map.get()->height-1 && mCore()->isDWater (tile_d) && mCore()->isDWater (tile_l) && mCore()->isWater(tile_dl))
                sdl = DOWNLEFT;
            if (x < m_map.get()->width-1 && y < m_map.get()->height-1 &&
                    mCore()->isDWater (tile_d) && mCore()->isDWater (tile_r) && mCore()->isWater(tile_dr))
                sdr = DOWNRIGHT;
        }
        _scode = sul+sur+sdl+sdr;
    }
    return mCore()->translate(terrain_id, _code, _scode);
}
