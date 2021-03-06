#include "messageswindow.h"
#include "ui_messageswindow.h"
#include "dialogwidget.h"
#include "vksdk.h"
#include "messagewidget.h"
#include <QObject>
#include <QScroller>
#include <wscrollarea.h>
#include "dialogwidget.h"
#include <QKeyEvent>
#include <QtMath>
#include "utils.h"
#include "longpoll.h"

MessagesWindow::MessagesWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MessagesWindow)
{
	ui->setupUi(this);
	longpoll.getLongPollServer();
	
	resizeTimer.setSingleShot( true );
	dialogs_manager = new QNetworkAccessManager();
	message_manager = new QNetworkAccessManager();
	history_manager = new QNetworkAccessManager();
	db = new InfoDatabase(parent);

	connect(dialogs_manager, SIGNAL(finished(QNetworkReply*)), SLOT(addDialogs(QNetworkReply*)));
	connect(ui->dialogsArea, SIGNAL(scrolledDown()), SLOT(loadupDialogs()));
	connect( &resizeTimer, SIGNAL(timeout()), SLOT(resizeUpdate()) );
	connect( &longpoll, SIGNAL(Message_New(const QJsonObject)), db, SLOT(slot_update(const QJsonObject)) );	
	connect( &longpoll, SIGNAL(Message_New(const QJsonObject)), SLOT(updateMessages(const QJsonObject)) );
	connect( message_manager, SIGNAL(finished(QNetworkReply*)), SLOT(messageSended(QNetworkReply*)));
	connect( ui->messageEdit, SIGNAL(sKeyPressEvent(QKeyEvent*)), SLOT(TextEditEvent(QKeyEvent*)));
	connect( history_manager, SIGNAL(finished(QNetworkReply*)), SLOT(messageHistory(QNetworkReply*)));
	connect( ui->messagesArea, SIGNAL(scrolledUp()), SLOT(loadupMessages()));
	connect( &conversation_avatar_loader, SIGNAL(downloaded(QString, int)), SLOT(conversation_avatar_downloaded(QString, int)) );
	connect( &message_avatar_loader, SIGNAL(downloaded(QString, int)), SLOT(message_avatar_downloaded(QString, int)) );

	
	m_iCurDialogCount = 0;
	m_iCurMessagesCount = 0;
	m_iDialogCount = 0;
	active_dialog = nullptr;
	requestDialogs(10);
	ui->messagesArea->m_bScrollDownNeed = true;
	
	conversation_avatar_loader.setDownloadDirectory(".image_previews");
	message_avatar_loader.setDownloadDirectory(".image_previews");
}

void MessagesWindow::conversation_avatar_downloaded(QString filename, int error)
{
	DialogWidget *d = nullptr;
	
	if( !error )
	{
		QStringList l = filename.split("/");
		for( int i = 0; i < ui->dialogsLayout->count(); i++ )
		{
			d = qobject_cast<DialogWidget *>(ui->dialogsLayout->itemAt(i)->widget());

			if( d && d->photo == l.last() )
					break;
		}

		QPixmap pix = QPixmap(filename);
		if( d ) d->setPhoto(pix);
		if( active_dialog == d )
			ui->dialogIcon->setPixmap(pix);
	}
}

void MessagesWindow::message_avatar_downloaded(QString filename, int error)
{
	QPixmap pix;
	if( !error && active_dialog )
	{
		if( !pix.load(filename) )
			return;
		
		QStringList l = filename.split("/");
		for( int i = 0; i < ui->messagesLayout->count(); i++)
		{
			messagewidget *m = qobject_cast<messagewidget*>(ui->messagesLayout->itemAt(i)->widget());
			
			if( m && m->photo == l.last() )
				m->setPhoto(pix);
		}
	}
}


void MessagesWindow::resizeEvent(QResizeEvent *event)
{
	resizeTimer.start( 500 );
	QMainWindow::resizeEvent(event);
}

void MessagesWindow::resizeUpdate()
{
	if( ui->dialogsArea->isScrolledDown() && m_iCurDialogCount < m_iDialogCount )
		requestDialogs(10, m_iCurDialogCount);
}

void MessagesWindow::requestDialogs(int count, int offset)
{
	QUrlQuery query
	{
		{"extended", "1"},
		{"fields", "photo_200"},
		{"count", QString::number(count)},
		{"offset", QString::number(offset)}
	};
	dialogs_manager->get(vkapi.method("messages.getConversations", query));	
}

void MessagesWindow::loadupDialogs()
{
	if( m_iCurDialogCount < m_iDialogCount )
		requestDialogs(10, m_iCurDialogCount);
}

void MessagesWindow::addDialogs(QNetworkReply *reply)
{
	QString msg_time, img_url;
	QPixmap pix;
	
	const QJsonObject jObj = QJsonDocument::fromJson(reply->readAll()).object();
//	db->update(jObj);

	if ( !jObj["response"].isUndefined() )
	{
		m_iDialogCount = jObj["response"]["count"].toInt();
		const QJsonArray items = jObj["response"]["items"].toArray();
		const QJsonArray profiles = jObj["response"]["profiles"].toArray();
		const QJsonArray groups = jObj["response"]["groups"].toArray();

		m_iCurDialogCount += items.count();
		for(int i = 0; i < items.count(); i++)
		{
			const QJsonObject conversation = items[i]["conversation"].toObject();

			QString title = items[i]["conversation"]["chat_settings"]["title"].toString();
			QString last_msg = items[i]["last_message"]["text"].toString();
			
			int unread = items[i]["conversation"]["unread_count"].toInt();
			
			msg_time = UTIL_TimestampToQStr(items[i]["last_message"]["date"].toInt());

			QJsonObject message = items[i]["last_message"].toObject();
			
			DialogWidget *dialogwidget = new DialogWidget(nullptr, title, last_msg, unread, msg_time );
			dialogwidget->peer_id = conversation["peer"]["id"].toInt();
			dialogwidget->type = items[i]["conversation"]["peer"]["type"].toString();		
			//dialogwidget->messages.append(message);
	
			connect( dialogwidget, SIGNAL(dialogSelected(DialogWidget *)), SLOT(dialogSelected(DialogWidget *)) );
			ui->dialogsLayout->addWidget(dialogwidget);

/*			
			if( dialogwidget->type == "user" )
			{
				profile_t profile = db->getProfile( dialogwidget->peer_id );
				dialogwidget->setDialogName(profile.first_name+" "+profile.last_name);
			}
			else if( dialogwidget->peer_id < 0 )
				dialogwidget->setDialogName(db->getGroup(qAbs(dialogwidget->peer_id)).name);
*/
			
			if( dialogwidget->type == "user")
			{
				for( int j = 0; j < profiles.count(); j++)
				{
					const QJsonObject profile = profiles[j].toObject();
					if( profile["id"].toInt() == dialogwidget->peer_id )
					{						
						if( !profile["photo_200"].isUndefined() )
						{
							QString img_url = profile["photo_200"].toString();
							dialogwidget->photo = img_url.split("/").last().split(".jpg")[0];
							dialogwidget->photo.truncate(20);

							if( pix.load(".image_previews/"+dialogwidget->photo) )
								dialogwidget->setPhoto(pix);
							else
								conversation_avatar_loader.append(img_url, dialogwidget->photo);	
						}
						dialogwidget->setDialogName(profile["first_name"].toString()+" "+profile["last_name"].toString());
						break;
					}
				}
			}
			else if( dialogwidget->type == "group")
			{
				for( int j = 0; j < groups.size(); j++)
				{
					const QJsonObject group = groups[j].toObject();
					
					if( group["id"].toInt() == -dialogwidget->peer_id )
					{
						if( !group["photo_200"].isUndefined() )
						{
							img_url = group["photo_200"].toString();
							dialogwidget->photo = img_url.split("/").last().split(".jpg")[0];
							dialogwidget->photo.truncate(20);
							if( pix.load(".image_previews/"+dialogwidget->photo) )
								dialogwidget->setPhoto(pix);
							else
								conversation_avatar_loader.append(img_url, dialogwidget->photo);	
						}
						dialogwidget->setDialogName(group["name"].toString());
						break;
					}
				}				
			}
			else if( dialogwidget->type == "chat" )
			{
				if( !conversation["chat_settings"]["photo"]["photo_200"].isUndefined() )
				{
					img_url = conversation["chat_settings"]["photo"]["photo_200"].toString();
					dialogwidget->photo = img_url.split("/").last().split(".jpg")[0];
					dialogwidget->photo.truncate(20);
					if( pix.load(".image_previews/"+dialogwidget->photo) )
						dialogwidget->setPhoto(pix);
					else
						conversation_avatar_loader.append(img_url, dialogwidget->photo);				
				}
			}
		}
		
		if( ui->dialogsArea->isScrolledDown() && m_iCurDialogCount < m_iDialogCount )
			requestDialogs(10, m_iCurDialogCount);
	}
	conversation_avatar_loader.download();
}

void MessagesWindow::updateMessages(const QJsonObject messages, bool bottom)
{
	QString img_url; QPixmap pix;
	const QJsonArray msgs = messages["response"]["items"].toArray();
	const QJsonArray profiles = messages["response"]["profiles"].toArray();
	const QJsonArray groups = messages["response"]["groups"].toArray();

	for(int i = 0; i < msgs.count();i++)
	{
		DialogWidget *widget = getDialogById(messages["response"]["items"][i]["peer_id"].toInt());
		if( widget )
		{
			const QJsonObject msg = msgs[i].toObject();
			if(bottom)
			{
				ui->dialogsLayout->removeWidget(widget);
				ui->dialogsLayout->insertWidget(0, widget);
				widget->setLastMessageText(msg["text"].toString());			
//				widget->messages.append(msg);
			}
//			else
//				widget->messages.insert(0, msg);
			m_iCurMessagesCount++;

			if( active_dialog && active_dialog == widget )
			{
				QString msg_time = UTIL_TimestampToQStr(msg["date"].toInt());
				
				int from_id = msg["from_id"].toInt();
				QString name;

/*
				if( from_id < 0 )
					name = db->getGroup(qAbs(id)).name;
				else
				{
					profile_t profile = db->getProfile(id);
					name = profile.first_name+" "+profile.last_name;
				}
*/

				messagewidget *message = new messagewidget(this, "", msg["text"].toString() , msg_time );

				if(bottom)
					ui->messagesLayout->addWidget(message);
				else
					ui->messagesLayout->insertWidget(0, message);				

				if( from_id > 0 )
				{
					for( int j = 0; j < profiles.count(); j++)
					{
						const QJsonObject profile = profiles[j].toObject();
//						qDebug() << profile;
						if( profile["id"].toInt() == from_id )
						{
							name = profile["first_name"].toString()+" "+profile["last_name"].toString();
							
							if( !profile["photo_200"].isUndefined() )
							{
								img_url = profile["photo_200"].toString();
								message->photo = img_url.split("/").last().split(".jpg")[0];
								message->photo.truncate(20);
								if( pix.load(".image_previews/"+message->photo) )
									message->setPhoto(pix);
								else if( !message_avatar_loader.queueExists(message->photo) )
									message_avatar_loader.append(img_url, message->photo);
							}
							break;	
						}
					}
				}
				else
				{
					for( int j = 0; j < groups.count(); j++)
					{
						const QJsonObject group = groups[j].toObject();
						
						if( group["id"].toInt() == -from_id )
						{
							name = group["name"].toString();
							
							if( !group["photo_200"].isUndefined() )
							{
								img_url = group["photo_200"].toString();
								message->photo = img_url.split("/").last().split(".jpg")[0];
								message->photo.truncate(20);
								if( pix.load(".image_previews/"+message->photo) )
									message->setPhoto(pix);
								else if( !message_avatar_loader.queueExists(message->photo) )
									message_avatar_loader.append(img_url, message->photo);	
							}
							break;
						}
					}
				}
				
				message->setName(name);
			}
		}
	}
	
	message_avatar_loader.download();
}

DialogWidget *MessagesWindow::getDialogById(int peer_id)
{
	for( int i = 0; i < ui->dialogsLayout->count(); i++ )
	{
		DialogWidget *widget = qobject_cast<DialogWidget *>(ui->dialogsLayout->itemAt(i)->widget());
		
		if( widget )
		{
			if( widget->peer_id == peer_id )
				return widget;
		}
	}
	return nullptr;
}

void clearLayout(QLayout* layout, bool deleteWidgets = true)
{
    while (QLayoutItem* item = layout->takeAt(0))
    {
        QWidget* widget;
        if (  (deleteWidgets)
              && (widget = item->widget())  ) {
            delete widget;
        }
        if (QLayout* childLayout = item->layout()) {
            clearLayout(childLayout, deleteWidgets);
        }
        delete item;
    }
}

void MessagesWindow::dialogSelected(DialogWidget *dialog)
{
	if( !dialog ) return;
	
	active_dialog = dialog;
	QList<QJsonObject>::iterator it; 
	
	clearLayout(ui->messagesLayout);
	ui->dialogIcon->setPixmap(QPixmap(".image_previews/"+dialog->photo));

/*	
	for( it = dialog->messages.begin(); it != dialog->messages.end(); it++)
	{
		QString name;
		int id = (*it)["from_id"].toInt();
		
		if( id < 0 )
			name = db->getGroup(qAbs(id)).name;
		else
		{
			profile_t profile = db->getProfile(id);
			name = profile.first_name+" "+profile.last_name;
		}
					
		QString msg_time = UTIL_TimestampToQStr((*it)["date"].toInt());

		QWidget *message = new messagewidget(this, name, (*it)["text"].toString(), msg_time );
		ui->messagesLayout->addWidget(message);
	}
*/
	
	m_iCurMessagesCount = 0;
	loadupMessages();
}

MessagesWindow::~MessagesWindow()
{
	delete ui;
}

void MessagesWindow::messageSended(QNetworkReply* reply)
{

}

void MessagesWindow::on_sendButton_released()
{
	if( !active_dialog )
		return;
	
	QUrlQuery query
	{
		{"message", ui->messageEdit->toPlainText()},
		{"random_id", "0"},
		{"peer_id", QString::number(active_dialog->peer_id) }
	};
	ui->messageEdit->clear();
    message_manager->get(vkapi.method("messages.send", query));
}

void MessagesWindow::TextEditEvent(QKeyEvent *event)
{
	if( event->type() == QKeyEvent::KeyPress )
	{		
		if( event->key() == Qt::Key_Return && !(QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier))
		{
			if( !active_dialog )
				return;
			
			QUrlQuery query
			{
				{"message", ui->messageEdit->toPlainText()},
				{"random_id", "0"},
				{"peer_id", QString::number(active_dialog->peer_id) }
			};
			ui->messageEdit->clear();
			message_manager->get(vkapi.method("messages.send", query));
		}
	}
}

void MessagesWindow::messageHistory(QNetworkReply *reply)
{
	const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
	updateMessages(obj, false);
}

void MessagesWindow::loadupMessages()
{
	if( !active_dialog )
		return;
	
	QUrlQuery query
	{
		{"count", "30"},
		{"offset", QString::number(m_iCurMessagesCount) },
		{"peer_id", QString::number(active_dialog->peer_id) },
		{"extended", "1"},
		{"fields", "photo_200"}
	};
	
	history_manager->get(vkapi.method("messages.getHistory", query));
}
