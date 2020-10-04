/*
    Copyright (C) 2020 Sebastian J. Wolf

    This file is part of Fernschreiber.

    Fernschreiber is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Fernschreiber is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Fernschreiber. If not, see <http://www.gnu.org/licenses/>.
*/

#include "tdlibwrapper.h"
#include "tdlibsecrets.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QSysInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QDBusConnection>
#include <QDBusInterface>

#define LOG(x) qDebug() << "[TDLibWrapper]" << x

#ifdef DEBUG
#  define VERBOSE(x) LOG(x)
#else
#  define VERBOSE(x)
#endif

namespace {
    const QString STATUS("status");
    const QString _TYPE("@type");
}

TDLibWrapper::TDLibWrapper(QObject *parent) : QObject(parent), settings("harbour-fernschreiber", "settings")
{
    LOG("Initializing TD Lib...");
    this->tdLibClient = td_json_client_create();
    this->tdLibReceiver = new TDLibReceiver(this->tdLibClient, this);

    QString tdLibDatabaseDirectoryPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tdlib";
    QDir tdLibDatabaseDirectory(tdLibDatabaseDirectoryPath);
    if (!tdLibDatabaseDirectory.exists()) {
        tdLibDatabaseDirectory.mkpath(tdLibDatabaseDirectoryPath);
    }

    this->dbusInterface = new DBusInterface(this);
    this->initializeOpenWith();

    connect(this->tdLibReceiver, SIGNAL(versionDetected(QString)), this, SLOT(handleVersionDetected(QString)));
    connect(this->tdLibReceiver, SIGNAL(authorizationStateChanged(QString)), this, SLOT(handleAuthorizationStateChanged(QString)));
    connect(this->tdLibReceiver, SIGNAL(optionUpdated(QString, QVariant)), this, SLOT(handleOptionUpdated(QString, QVariant)));
    connect(this->tdLibReceiver, SIGNAL(connectionStateChanged(QString)), this, SLOT(handleConnectionStateChanged(QString)));
    connect(this->tdLibReceiver, SIGNAL(userUpdated(QVariantMap)), this, SLOT(handleUserUpdated(QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(userStatusUpdated(QString, QVariantMap)), this, SLOT(handleUserStatusUpdated(QString, QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(fileUpdated(QVariantMap)), this, SLOT(handleFileUpdated(QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(newChatDiscovered(QVariantMap)), this, SLOT(handleNewChatDiscovered(QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(unreadMessageCountUpdated(QVariantMap)), this, SLOT(handleUnreadMessageCountUpdated(QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(unreadChatCountUpdated(QVariantMap)), this, SLOT(handleUnreadChatCountUpdated(QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(chatLastMessageUpdated(QString, QString, QVariantMap)), this, SLOT(handleChatLastMessageUpdated(QString, QString, QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(chatOrderUpdated(QString, QString)), this, SLOT(handleChatOrderUpdated(QString, QString)));
    connect(this->tdLibReceiver, SIGNAL(chatReadInboxUpdated(QString, QString, int)), this, SLOT(handleChatReadInboxUpdated(QString, QString, int)));
    connect(this->tdLibReceiver, SIGNAL(chatReadOutboxUpdated(QString, QString)), this, SLOT(handleChatReadOutboxUpdated(QString, QString)));
    connect(this->tdLibReceiver, SIGNAL(basicGroupUpdated(qlonglong, QVariantMap)), this, SLOT(handleBasicGroupUpdated(qlonglong, QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(superGroupUpdated(qlonglong, QVariantMap)), this, SLOT(handleSuperGroupUpdated(qlonglong, QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(chatOnlineMemberCountUpdated(QString, int)), this, SLOT(handleChatOnlineMemberCountUpdated(QString, int)));
    connect(this->tdLibReceiver, SIGNAL(messagesReceived(QVariantList)), this, SLOT(handleMessagesReceived(QVariantList)));
    connect(this->tdLibReceiver, SIGNAL(newMessageReceived(QString, QVariantMap)), this, SLOT(handleNewMessageReceived(QString, QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(messageInformation(QString, QVariantMap)), this, SLOT(handleMessageInformation(QString, QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(messageSendSucceeded(QString, QString, QVariantMap)), this, SLOT(handleMessageSendSucceeded(QString, QString, QVariantMap)));    
    connect(this->tdLibReceiver, SIGNAL(activeNotificationsUpdated(QVariantList)), this, SLOT(handleUpdateActiveNotifications(QVariantList)));
    connect(this->tdLibReceiver, SIGNAL(notificationGroupUpdated(QVariantMap)), this, SLOT(handleUpdateNotificationGroup(QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(notificationUpdated(QVariantMap)), this, SLOT(handleUpdateNotification(QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(chatNotificationSettingsUpdated(QString, QVariantMap)), this, SLOT(handleChatNotificationSettingsUpdated(QString, QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(messageContentUpdated(QString, QString, QVariantMap)), this, SLOT(handleMessageContentUpdated(QString, QString, QVariantMap)));
    connect(this->tdLibReceiver, SIGNAL(messagesDeleted(QString, QVariantList)), this, SLOT(handleMessagesDeleted(QString, QVariantList)));

    this->tdLibReceiver->start();

    this->setLogVerbosityLevel();
    this->setOptionInteger("notification_group_count_max", 5);
}

TDLibWrapper::~TDLibWrapper()
{
    LOG("Destroying TD Lib...");
    this->tdLibReceiver->setActive(false);
    while (this->tdLibReceiver->isRunning()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1000);
    }
    qDeleteAll(basicGroups.values());
    qDeleteAll(superGroups.values());
    td_json_client_destroy(this->tdLibClient);
}

void TDLibWrapper::sendRequest(const QVariantMap &requestObject)
{
    LOG("Sending request to TD Lib, object type name:" << requestObject.value(_TYPE).toString());
    QJsonDocument requestDocument = QJsonDocument::fromVariant(requestObject);
    VERBOSE(requestDocument.toJson().constData());
    td_json_client_send(this->tdLibClient, requestDocument.toJson().constData());
}

QString TDLibWrapper::getVersion()
{
    return this->version;
}

TDLibWrapper::AuthorizationState TDLibWrapper::getAuthorizationState()
{
    return this->authorizationState;
}

TDLibWrapper::ConnectionState TDLibWrapper::getConnectionState()
{
    return this->connectionState;
}

void TDLibWrapper::setAuthenticationPhoneNumber(const QString &phoneNumber)
{
    LOG("Set authentication phone number " << phoneNumber);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "setAuthenticationPhoneNumber");
    requestObject.insert("phone_number", phoneNumber);
    QVariantMap phoneNumberSettings;
    phoneNumberSettings.insert("allow_flash_call", false);
    phoneNumberSettings.insert("is_current_phone_number", true);
    requestObject.insert("settings", phoneNumberSettings);
    this->sendRequest(requestObject);
}

void TDLibWrapper::setAuthenticationCode(const QString &authenticationCode)
{
    LOG("Set authentication code " << authenticationCode);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "checkAuthenticationCode");
    requestObject.insert("code", authenticationCode);
    this->sendRequest(requestObject);
}

void TDLibWrapper::setAuthenticationPassword(const QString &authenticationPassword)
{
    LOG("Set authentication password " << authenticationPassword);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "checkAuthenticationPassword");
    requestObject.insert("password", authenticationPassword);
    this->sendRequest(requestObject);
}

void TDLibWrapper::getChats()
{
    LOG("Getting chats");
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "getChats");
    requestObject.insert("limit", 5);
    this->sendRequest(requestObject);
}

void TDLibWrapper::downloadFile(const QString &fileId)
{
    LOG("Downloading file " << fileId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "downloadFile");
    requestObject.insert("file_id", fileId);
    requestObject.insert("synchronous", false);
    requestObject.insert("offset", 0);
    requestObject.insert("limit", 0);
    requestObject.insert("priority", 1);
    this->sendRequest(requestObject);
}

void TDLibWrapper::openChat(const QString &chatId)
{
    LOG("Opening chat " << chatId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "openChat");
    requestObject.insert("chat_id", chatId);
    this->sendRequest(requestObject);
}

void TDLibWrapper::closeChat(const QString &chatId)
{
    LOG("Closing chat " << chatId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "closeChat");
    requestObject.insert("chat_id", chatId);
    this->sendRequest(requestObject);
}

void TDLibWrapper::getChatHistory(const QString &chatId, const qlonglong &fromMessageId, const int &offset, const int &limit, const bool &onlyLocal)
{
    LOG("Retrieving chat history" << chatId << fromMessageId << offset << limit << onlyLocal);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "getChatHistory");
    requestObject.insert("chat_id", chatId);
    requestObject.insert("from_message_id", fromMessageId);
    requestObject.insert("offset", offset);
    requestObject.insert("limit", limit);
    requestObject.insert("only_local", onlyLocal);
    this->sendRequest(requestObject);
}

void TDLibWrapper::viewMessage(const QString &chatId, const QString &messageId)
{
    LOG("Mark message as viewed" << chatId << messageId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "viewMessages");
    requestObject.insert("chat_id", chatId);
    requestObject.insert("force_read", false);
    QVariantList messageIds;
    messageIds.append(messageId);
    requestObject.insert("message_ids", messageIds);
    this->sendRequest(requestObject);
}

void TDLibWrapper::sendTextMessage(const QString &chatId, const QString &message, const QString &replyToMessageId)
{
    LOG("Sending text message" << chatId << message << replyToMessageId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "sendMessage");
    requestObject.insert("chat_id", chatId);
    if (replyToMessageId != "0") {
        requestObject.insert("reply_to_message_id", replyToMessageId);
    }
    QVariantMap inputMessageContent;
    inputMessageContent.insert(_TYPE, "inputMessageText");
    QVariantMap formattedText;
    formattedText.insert("text", message);
    formattedText.insert(_TYPE, "formattedText");
    inputMessageContent.insert("text", formattedText);
    requestObject.insert("input_message_content", inputMessageContent);
    this->sendRequest(requestObject);
}

void TDLibWrapper::sendPhotoMessage(const QString &chatId, const QString &filePath, const QString &message, const QString &replyToMessageId)
{
    LOG("Sending photo message" << chatId << filePath << message << replyToMessageId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "sendMessage");
    requestObject.insert("chat_id", chatId);
    if (replyToMessageId != "0") {
        requestObject.insert("reply_to_message_id", replyToMessageId);
    }
    QVariantMap inputMessageContent;
    inputMessageContent.insert(_TYPE, "inputMessagePhoto");
    QVariantMap formattedText;
    formattedText.insert("text", message);
    formattedText.insert(_TYPE, "formattedText");
    inputMessageContent.insert("caption", formattedText);
    QVariantMap photoInputFile;
    photoInputFile.insert(_TYPE, "inputFileLocal");
    photoInputFile.insert("path", filePath);
    inputMessageContent.insert("photo", photoInputFile);

    requestObject.insert("input_message_content", inputMessageContent);
    this->sendRequest(requestObject);
}

void TDLibWrapper::sendVideoMessage(const QString &chatId, const QString &filePath, const QString &message, const QString &replyToMessageId)
{
    LOG("Sending video message" << chatId << filePath << message << replyToMessageId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "sendMessage");
    requestObject.insert("chat_id", chatId);
    if (replyToMessageId != "0") {
        requestObject.insert("reply_to_message_id", replyToMessageId);
    }
    QVariantMap inputMessageContent;
    inputMessageContent.insert(_TYPE, "inputMessageVideo");
    QVariantMap formattedText;
    formattedText.insert("text", message);
    formattedText.insert(_TYPE, "formattedText");
    inputMessageContent.insert("caption", formattedText);
    QVariantMap videoInputFile;
    videoInputFile.insert(_TYPE, "inputFileLocal");
    videoInputFile.insert("path", filePath);
    inputMessageContent.insert("video", videoInputFile);

    requestObject.insert("input_message_content", inputMessageContent);
    this->sendRequest(requestObject);
}

void TDLibWrapper::sendDocumentMessage(const QString &chatId, const QString &filePath, const QString &message, const QString &replyToMessageId)
{
    LOG("Sending document message" << chatId << filePath << message << replyToMessageId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "sendMessage");
    requestObject.insert("chat_id", chatId);
    if (replyToMessageId != "0") {
        requestObject.insert("reply_to_message_id", replyToMessageId);
    }
    QVariantMap inputMessageContent;
    inputMessageContent.insert(_TYPE, "inputMessageDocument");
    QVariantMap formattedText;
    formattedText.insert("text", message);
    formattedText.insert(_TYPE, "formattedText");
    inputMessageContent.insert("caption", formattedText);
    QVariantMap documentInputFile;
    documentInputFile.insert(_TYPE, "inputFileLocal");
    documentInputFile.insert("path", filePath);
    inputMessageContent.insert("document", documentInputFile);

    requestObject.insert("input_message_content", inputMessageContent);
    this->sendRequest(requestObject);
}

void TDLibWrapper::getMessage(const QString &chatId, const QString &messageId)
{
    LOG("Retrieving message" << chatId << messageId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "getMessage");
    requestObject.insert("chat_id", chatId);
    requestObject.insert("message_id", messageId);
    this->sendRequest(requestObject);
}

void TDLibWrapper::setOptionInteger(const QString &optionName, const int &optionValue)
{
    LOG("Setting integer option" << optionName << optionValue);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "setOption");
    requestObject.insert("name", optionName);
    QVariantMap optionValueMap;
    optionValueMap.insert(_TYPE, "optionValueInteger");
    optionValueMap.insert("value", optionValue);
    requestObject.insert("value", optionValueMap);
    this->sendRequest(requestObject);
}

void TDLibWrapper::setChatNotificationSettings(const QString &chatId, const QVariantMap &notificationSettings)
{
    LOG("Notification settings for chat " << chatId << notificationSettings);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "setChatNotificationSettings");
    requestObject.insert("chat_id", chatId);
    requestObject.insert("notification_settings", notificationSettings);
    this->sendRequest(requestObject);
}

void TDLibWrapper::editMessageText(const QString &chatId, const QString &messageId, const QString &message)
{
    LOG("Editing message text" << chatId << messageId);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "editMessageText");
    requestObject.insert("chat_id", chatId);
    requestObject.insert("message_id", messageId);
    QVariantMap inputMessageContent;
    inputMessageContent.insert(_TYPE, "inputMessageText");
    QVariantMap formattedText;
    formattedText.insert("text", message);
    inputMessageContent.insert("text", formattedText);
    requestObject.insert("input_message_content", inputMessageContent);
    this->sendRequest(requestObject);
}

void TDLibWrapper::deleteMessages(const QString &chatId, const QVariantList messageIds)
{
    LOG("Deleting some messages" << chatId << messageIds);
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "deleteMessages");
    requestObject.insert("chat_id", chatId);
    requestObject.insert("message_ids", messageIds);
    requestObject.insert("revoke", true);
    this->sendRequest(requestObject);
}

void TDLibWrapper::getMapThumbnailFile(const QString &chatId, const double &latitude, const double &longitude, const int &width, const int &height)
{
    LOG("Getting Map Thumbnail File" << chatId);
    QVariantMap location;
    location.insert("latitude", latitude);
    location.insert("longitude", longitude);

    // ensure dimensions are in bounds (16 - 1024)
    int boundsWidth = std::min(std::max(width, 16), 1024);
    int boundsHeight = std::min(std::max(height, 16), 1024);

    QVariantMap requestObject;
    requestObject.insert(_TYPE, "getMapThumbnailFile");
    requestObject.insert("location", location);
    requestObject.insert("zoom", 17); //13-20
    requestObject.insert("width", boundsWidth);
    requestObject.insert("height", boundsHeight);
    requestObject.insert("scale", 1); // 1-3
    requestObject.insert("chat_id", chatId);

    this->sendRequest(requestObject);
}

QVariantMap TDLibWrapper::getUserInformation()
{
    return this->userInformation;
}

QVariantMap TDLibWrapper::getUserInformation(const QString &userId)
{
    // LOG("Returning user information for ID" << userId);
    return this->allUsers.value(userId).toMap();
}

QVariantMap TDLibWrapper::getUnreadMessageInformation()
{
    return this->unreadMessageInformation;
}

QVariantMap TDLibWrapper::getUnreadChatInformation()
{
    return this->unreadChatInformation;
}

QVariantMap TDLibWrapper::getBasicGroup(qlonglong groupId) const
{
    const Group* group = basicGroups.value(groupId);
    if (group) {
        LOG("Returning basic group information for ID" << groupId);
        return group->groupInfo;
    } else {
        LOG("No super group information for ID" << groupId);
        return QVariantMap();
    }
}

QVariantMap TDLibWrapper::getSuperGroup(qlonglong groupId) const
{
    const Group* group = superGroups.value(groupId);
    if (group) {
        LOG("Returning super group information for ID" << groupId);
        return group->groupInfo;
    } else {
        LOG("No super group information for ID" << groupId);
        return QVariantMap();
    }
}

QVariantMap TDLibWrapper::getChat(const QString &chatId)
{
    LOG("Returning chat information for ID" << chatId);
    return this->chats.value(chatId).toMap();
}

void TDLibWrapper::copyFileToDownloads(const QString &filePath)
{
    LOG("Copy file to downloads" << filePath);
    QFileInfo fileInfo(filePath);
    if (fileInfo.exists()) {
        QString downloadFilePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + fileInfo.fileName();
        if (QFile::copy(filePath, downloadFilePath)) {
            emit copyToDownloadsSuccessful(fileInfo.fileName(), downloadFilePath);
        } else {
            emit copyToDownloadsError(fileInfo.fileName(), downloadFilePath);
        }
    } else {
        emit copyToDownloadsError(fileInfo.fileName(), filePath);
    }
}

void TDLibWrapper::openFileOnDevice(const QString &filePath)
{
    LOG("Open file on device:" << filePath);
    QStringList argumentsList;
    argumentsList.append(filePath);
    bool successfullyStarted = QProcess::startDetached("xdg-open", argumentsList);
    if (successfullyStarted) {
        qDebug() << "Successfully opened file " << filePath;
    } else {
        qDebug() << "Error opening file " << filePath;
    }
}

void TDLibWrapper::controlScreenSaver(const bool &enabled)
{
    LOG("Controlling device screen saver" << enabled);
    QDBusConnection dbusConnection = QDBusConnection::connectToBus(QDBusConnection::SystemBus, "system");
    QDBusInterface dbusInterface("com.nokia.mce", "/com/nokia/mce/request", "com.nokia.mce.request", dbusConnection);

    if (enabled) {
        qDebug() << "Enabling screensaver";
        dbusInterface.call("req_display_cancel_blanking_pause");
    } else {
        qDebug() << "Disabling screensaver";
        dbusInterface.call("req_display_blanking_pause");
    }
}

void TDLibWrapper::setSendByEnter(const bool &sendByEnter)
{
    settings.setValue("sendByEnter", sendByEnter);
}

bool TDLibWrapper::getSendByEnter()
{
    return settings.value("sendByEnter", false).toBool();
}

DBusAdaptor *TDLibWrapper::getDBusAdaptor()
{
    return this->dbusInterface->getDBusAdaptor();
}

void TDLibWrapper::handleVersionDetected(const QString &version)
{
    this->version = version;
    emit versionDetected(version);
}

void TDLibWrapper::handleAuthorizationStateChanged(const QString &authorizationState)
{
    if (authorizationState == "authorizationStateClosed") {
        this->authorizationState = AuthorizationState::Closed;
    }

    if (authorizationState == "authorizationStateClosing") {
        this->authorizationState = AuthorizationState::Closing;
    }

    if (authorizationState == "authorizationStateLoggingOut") {
        this->authorizationState = AuthorizationState::LoggingOut;
    }

    if (authorizationState == "authorizationStateReady") {
        this->authorizationState = AuthorizationState::AuthorizationReady;
    }

    if (authorizationState == "authorizationStateWaitCode") {
        this->authorizationState = AuthorizationState::WaitCode;
    }

    if (authorizationState == "authorizationStateWaitEncryptionKey") {
        this->setEncryptionKey();
        this->authorizationState = AuthorizationState::WaitEncryptionKey;
    }

    if (authorizationState == "authorizationStateWaitOtherDeviceConfirmation") {
        this->authorizationState = AuthorizationState::WaitOtherDeviceConfirmation;
    }

    if (authorizationState == "authorizationStateWaitPassword") {
        this->authorizationState = AuthorizationState::WaitPassword;
    }

    if (authorizationState == "authorizationStateWaitPhoneNumber") {
        this->authorizationState = AuthorizationState::WaitPhoneNumber;
    }

    if (authorizationState == "authorizationStateWaitRegistration") {
        this->authorizationState = AuthorizationState::WaitRegistration;
    }

    if (authorizationState == "authorizationStateWaitTdlibParameters") {
        this->setInitialParameters();
        this->authorizationState = AuthorizationState::WaitTdlibParameters;
    }

    emit authorizationStateChanged(this->authorizationState);

}

void TDLibWrapper::handleOptionUpdated(const QString &optionName, const QVariant &optionValue)
{
    this->options.insert(optionName, optionValue);
    emit optionUpdated(optionName, optionValue);
    if (optionName == "my_id") {
        emit ownUserIdFound(optionValue.toString());
    }
}

void TDLibWrapper::handleConnectionStateChanged(const QString &connectionState)
{
    if (connectionState == "connectionStateConnecting") {
        this->connectionState = ConnectionState::Connecting;
    }
    if (connectionState == "connectionStateConnectingToProxy") {
        this->connectionState = ConnectionState::ConnectingToProxy;
    }
    if (connectionState == "connectionStateReady") {
        this->connectionState = ConnectionState::ConnectionReady;
    }
    if (connectionState == "connectionStateUpdating") {
        this->connectionState = ConnectionState::Updating;
    }
    if (connectionState == "connectionStateWaitingForNetwork") {
        this->connectionState = ConnectionState::WaitingForNetwork;
    }

    emit connectionStateChanged(this->connectionState);
}

void TDLibWrapper::handleUserUpdated(const QVariantMap &userInformation)
{
    QString updatedUserId = userInformation.value("id").toString();
    if (updatedUserId == this->options.value("my_id").toString()) {
        LOG("Own user information updated :)");
        this->userInformation = userInformation;
    }
    LOG("User information updated:" << userInformation.value("username").toString() << userInformation.value("first_name").toString() << userInformation.value("last_name").toString());
    this->allUsers.insert(updatedUserId, userInformation);
    emit userUpdated(updatedUserId, userInformation);
}

void TDLibWrapper::handleUserStatusUpdated(const QString &userId, const QVariantMap &userStatusInformation)
{
    if (userId == this->options.value("my_id").toString()) {
        LOG("Own user status information updated :)");
        this->userInformation.insert("status", userStatusInformation);
    }
    LOG("User status information updated:" << userId << userStatusInformation.value(_TYPE).toString());
    QVariantMap updatedUserInformation = this->allUsers.value(userId).toMap();
    updatedUserInformation.insert("status", userStatusInformation);
    this->allUsers.insert(userId, updatedUserInformation);
    emit userUpdated(userId, updatedUserInformation);
}

void TDLibWrapper::handleFileUpdated(const QVariantMap &fileInformation)
{
    emit fileUpdated(fileInformation.value("id").toInt(), fileInformation);
}

void TDLibWrapper::handleNewChatDiscovered(const QVariantMap &chatInformation)
{
    QString chatId = chatInformation.value("id").toString();
    this->chats.insert(chatId, chatInformation);
    emit newChatDiscovered(chatId, chatInformation);
}

void TDLibWrapper::handleUnreadMessageCountUpdated(const QVariantMap &messageCountInformation)
{
    if (messageCountInformation.value("chat_list_type").toString() == "chatListMain") {
        this->unreadMessageInformation = messageCountInformation;
        emit unreadMessageCountUpdated(messageCountInformation);
    }
}

void TDLibWrapper::handleUnreadChatCountUpdated(const QVariantMap &chatCountInformation)
{
    if (chatCountInformation.value("chat_list_type").toString() == "chatListMain") {
        this->unreadChatInformation = chatCountInformation;
        emit unreadChatCountUpdated(chatCountInformation);
    }
}

void TDLibWrapper::handleChatLastMessageUpdated(const QString &chatId, const QString &order, const QVariantMap &lastMessage)
{
    emit chatLastMessageUpdated(chatId, order, lastMessage);
}

void TDLibWrapper::handleChatOrderUpdated(const QString &chatId, const QString &order)
{
    emit chatOrderUpdated(chatId, order);
}

void TDLibWrapper::handleChatReadInboxUpdated(const QString &chatId, const QString &lastReadInboxMessageId, const int &unreadCount)
{
    emit chatReadInboxUpdated(chatId, lastReadInboxMessageId, unreadCount);
}

void TDLibWrapper::handleChatReadOutboxUpdated(const QString &chatId, const QString &lastReadOutboxMessageId)
{
    emit chatReadOutboxUpdated(chatId, lastReadOutboxMessageId);
}

void TDLibWrapper::handleBasicGroupUpdated(qlonglong groupId, const QVariantMap &groupInformation)
{
    emit basicGroupUpdated(updateGroup(groupId, groupInformation, &basicGroups)->groupId);
}

void TDLibWrapper::handleSuperGroupUpdated(qlonglong groupId, const QVariantMap &groupInformation)
{
    emit superGroupUpdated(updateGroup(groupId, groupInformation, &superGroups)->groupId);
}

void TDLibWrapper::handleChatOnlineMemberCountUpdated(const QString &chatId, const int &onlineMemberCount)
{
    emit chatOnlineMemberCountUpdated(chatId, onlineMemberCount);
}

void TDLibWrapper::handleMessagesReceived(const QVariantList &messages)
{
    emit messagesReceived(messages);
}

void TDLibWrapper::handleNewMessageReceived(const QString &chatId, const QVariantMap &message)
{
    emit newMessageReceived(chatId, message);
}

void TDLibWrapper::handleMessageInformation(const QString &messageId, const QVariantMap &message)
{
    emit receivedMessage(messageId, message);
}

void TDLibWrapper::handleMessageSendSucceeded(const QString &messageId, const QString &oldMessageId, const QVariantMap &message)
{
    emit messageSendSucceeded(messageId, oldMessageId, message);
}

void TDLibWrapper::handleUpdateActiveNotifications(const QVariantList notificationGroups)
{
    emit activeNotificationsUpdated(notificationGroups);
}

void TDLibWrapper::handleUpdateNotificationGroup(const QVariantMap notificationGroupUpdate)
{
    emit notificationGroupUpdated(notificationGroupUpdate);
}

void TDLibWrapper::handleUpdateNotification(const QVariantMap updatedNotification)
{
    emit notificationUpdated(updatedNotification);
}

void TDLibWrapper::handleChatNotificationSettingsUpdated(const QString &chatId, const QVariantMap &chatNotificationSettings)
{
    emit chatNotificationSettingsUpdated(chatId, chatNotificationSettings);
}

void TDLibWrapper::handleMessageContentUpdated(const QString &chatId, const QString &messageId, const QVariantMap &newContent)
{
    emit messageContentUpdated(chatId, messageId, newContent);
}

void TDLibWrapper::handleMessagesDeleted(const QString &chatId, const QVariantList &messageIds)
{
    emit messagesDeleted(chatId, messageIds);
}

void TDLibWrapper::setInitialParameters()
{
    LOG("Sending initial parameters to TD Lib");
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "setTdlibParameters");
    QVariantMap initialParameters;
    initialParameters.insert("api_id", TDLIB_API_ID);
    initialParameters.insert("api_hash", TDLIB_API_HASH);
    initialParameters.insert("database_directory", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tdlib");
    initialParameters.insert("use_file_database", true);
    initialParameters.insert("use_chat_info_database", true);
    initialParameters.insert("use_message_database", true);
    initialParameters.insert("use_secret_chats", false);
    initialParameters.insert("system_language_code", QLocale::system().name());
    QSettings hardwareSettings("/etc/hw-release", QSettings::NativeFormat);
    initialParameters.insert("device_model", hardwareSettings.value("NAME", "Unknown Mobile Device").toString());
    initialParameters.insert("system_version", QSysInfo::prettyProductName());
    initialParameters.insert("application_version", "0.2");
    requestObject.insert("parameters", initialParameters);
    this->sendRequest(requestObject);
}

void TDLibWrapper::setEncryptionKey()
{
    LOG("Setting database encryption key");
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "checkDatabaseEncryptionKey");
    // see https://github.com/tdlib/td/issues/188#issuecomment-379536139
    requestObject.insert("encryption_key", "");
    this->sendRequest(requestObject);
}

void TDLibWrapper::setLogVerbosityLevel()
{
    LOG("Setting log verbosity level to something less chatty");
    QVariantMap requestObject;
    requestObject.insert(_TYPE, "setLogVerbosityLevel");
    requestObject.insert("new_verbosity_level", 2);
    this->sendRequest(requestObject);
}

void TDLibWrapper::initializeOpenWith()
{
    LOG("Initialize open-with");

    QString dbusPathName = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/dbus-1/services";
    QDir dbusPath(dbusPathName);
    if (!dbusPath.exists()) {
        LOG("Creating D-Bus directory" << dbusPathName);
        dbusPath.mkpath(dbusPathName);
    }
    QString dbusServiceFileName = dbusPathName + "/de.ygriega.fernschreiber.service";
    QFile dbusServiceFile(dbusServiceFileName);
    if (!dbusServiceFile.exists()) {
        LOG("Creating D-Bus service file at" << dbusServiceFile.fileName());
        if (dbusServiceFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream fileOut(&dbusServiceFile);
            fileOut.setCodec("UTF-8");
            fileOut << QString("[D-BUS Service]").toUtf8() << "\n";
            fileOut << QString("Name=de.ygriega.fernschreiber").toUtf8() << "\n";
            fileOut << QString("Exec=/usr/bin/invoker -s --type=silica-qt5 /usr/bin/harbour-fernschreiber").toUtf8() << "\n";
            fileOut.flush();
            dbusServiceFile.close();
        }
    }
}

const TDLibWrapper::Group *TDLibWrapper::updateGroup(qlonglong groupId, const QVariantMap &groupInfo, QHash<qlonglong,Group*> *groups)
{
    Group* group = groups->value(groupId);
    if (!group) {
        group = new Group(groupId);
        groups->insert(groupId, group);
    }
    group->groupInfo = groupInfo;
    return group;
}

const TDLibWrapper::Group* TDLibWrapper::getGroup(qlonglong groupId) const
{
    if (groupId) {
        const Group* group = superGroups.value(groupId);
        return group ? group : basicGroups.value(groupId);
    }
    return Q_NULLPTR;
}

TDLibWrapper::ChatMemberStatus TDLibWrapper::chatMemberStatusFromString(const QString &status)
{
    // Most common ones first
    return (status == QStringLiteral("chatMemberStatusMember")) ? ChatMemberStatusMember :
        (status == QStringLiteral("chatMemberStatusLeft")) ? ChatMemberStatusLeft :
        (status == QStringLiteral("chatMemberStatusCreator")) ? ChatMemberStatusCreator :
        (status == QStringLiteral("chatMemberStatusAdministrator")) ?  ChatMemberStatusAdministrator :
        (status == QStringLiteral("chatMemberStatusRestricted")) ? ChatMemberStatusRestricted :
        (status == QStringLiteral("chatMemberStatusBanned")) ?  ChatMemberStatusBanned :
        ChatMemberStatusUnknown;
}

TDLibWrapper::ChatMemberStatus TDLibWrapper::Group::chatMemberStatus() const
{
    const QString statusType(groupInfo.value(STATUS).toMap().value(_TYPE).toString());
    return statusType.isEmpty() ? ChatMemberStatusUnknown : chatMemberStatusFromString(statusType);
}
