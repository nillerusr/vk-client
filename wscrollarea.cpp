#include "wscrollarea.h"
#include <QScrollBar>
#include <QtCore>

WScrollArea::WScrollArea( QWidget *parent ) : QScrollArea(parent)
{
	QScrollBar* scrollbar = verticalScrollBar();
	QObject::connect(scrollbar, SIGNAL(rangeChanged(int,int)), this, SLOT(moveScrollBarToBottom(int, int)));
	m_bScrollDownNeed = false;
	m_bScrolledDown = true;
}

void WScrollArea::scrollContentsBy(int dx, int dy)
{
	if(this->verticalScrollBar()->value() == this->verticalScrollBar()->maximum())
		m_bScrolledDown = true;
	else
		m_bScrolledDown = false;
	
    QScrollArea::scrollContentsBy(dx, dy);
		
	if(this->verticalScrollBar()->value() == this->verticalScrollBar()->maximum())
		emit scrolledDown();
	else if( this->verticalScrollBar()->value() == this->verticalScrollBar()->minimum() )
		emit scrolledUp();
}

bool WScrollArea::isScrolledDown()
{
	if(this->verticalScrollBar()->value() == this->verticalScrollBar()->maximum())
		return true;
	return false;
}

bool WScrollArea::isScrolledUp()
{
	if( this->verticalScrollBar()->value() == this->verticalScrollBar()->minimum() )
		return true;
	return false;
}

void WScrollArea::moveScrollBarToBottom(int min, int max)
{
    Q_UNUSED(min);
	if( m_bScrollDownNeed && m_bScrolledDown )
		verticalScrollBar()->setValue(max);
}
