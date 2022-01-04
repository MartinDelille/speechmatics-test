#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>

using namespace std;

QString createJob(const QString &token,
                  const QString &mediaUrl,
                  const QString &language,
                  const QString &scriptFileName) {
	QString id;
	auto multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
	QHttpPart configPart;
	configPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));
	configPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"config\""));
	QJsonObject configObject;

	auto scriptFile = new QFile(scriptFileName);

	if (scriptFile->exists()) {
		configObject["type"] = "alignment";
		QJsonObject alignmentConfigObject;
		alignmentConfigObject["language"] = language;
		alignmentConfigObject["operating_point"] = "enhanced";
		configObject["alignment_config"] = alignmentConfigObject;

		if (scriptFile->open(QFile::ReadOnly | QFile::Text)) {
			QFileInfo scriptFileInfo(scriptFileName);
			QHttpPart scriptPart;
			qDebug() << scriptFileInfo.fileName();
			scriptPart.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"text_file\"; filename=\"%0\"").arg(scriptFileInfo.fileName()));
			scriptPart.setBodyDevice(scriptFile);
			scriptFile->setParent(multiPart);
			multiPart->append(scriptPart);
		} else {
			delete scriptFile;
			qCritical() << "Unable to open" << scriptFileName;
			return "";
		}
	} else {
		delete scriptFile;
		configObject["type"] = "transcription";
		QJsonObject transcriptionConfigObject;
		transcriptionConfigObject["language"] = language;
		transcriptionConfigObject["operating_point"] = "enhanced";
		configObject["transcription_config"] = transcriptionConfigObject;
	}

	if (QFile::exists(mediaUrl)) {
		QFileInfo mediaFileInfo(mediaUrl);
		auto mediaFile = new QFile(mediaUrl);

		if (mediaFile->open(QFile::ReadOnly)) {
			QHttpPart mediaPart;
			mediaPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
			qDebug() << mediaFileInfo.fileName();
			mediaPart.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"data_file\"; filename=\"%0\"").arg(mediaFileInfo.fileName()));
			mediaPart.setBodyDevice(mediaFile);
			mediaFile->setParent(multiPart);
			multiPart->append(mediaPart);
		} else {
			qCritical() << "Unable to open" << mediaUrl;
		}
	} else {
		QJsonObject fetchDataObject;
		fetchDataObject["url"] = mediaUrl;
		configObject["fetch_data"] = fetchDataObject;
	}

	QJsonDocument body(configObject);
	qDebug().noquote() << body.toJson(QJsonDocument::Indented);
	configPart.setBody(body.toJson());
	multiPart->append(configPart);
	qDebug() << multiPart->boundary();

	QUrl jobUrl("https://trial.asr.api.speechmatics.com/v2/jobs");
	QNetworkRequest request(jobUrl);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

	request.setRawHeader("Authorization", QString("Bearer %0").arg(token).toUtf8());

	QNetworkAccessManager manager;
	QEventLoop loop;
	auto reply = manager.post(request, multiPart);
	multiPart->setParent(reply);
	QObject::connect(reply, &QNetworkReply::uploadProgress, [](qint64 bytesSent, qint64 bytesTotal) {
		qDebug() << bytesSent << "/" << bytesTotal;
	});

	QObject::connect(reply, &QNetworkReply::finished, [&loop, reply, &id] {
		auto content = QJsonDocument::fromJson(reply->readAll());
		qDebug().noquote() << content.toJson(QJsonDocument::Indented);

		if (reply->error() == QNetworkReply::NoError) {
			id = content.object()["id"].toString();
		} else {
			qCritical() << reply->errorString();

			for (auto header : reply->rawHeaderList()) {
				qCritical() << header << ":" << reply->rawHeader(header);
			}
		}

		loop.exit();
	});
	loop.exec();

	return id;
}

bool checkJob(const QString &token,
              const QString &id) {
	bool result = false;
	QUrl url(QString("https://trial.asr.api.speechmatics.com/v2/jobs/%0").arg(id));
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("Authorization", QString("Bearer %0").arg(token).toUtf8());
	QNetworkAccessManager manager;
	QEventLoop loop;
	auto reply = manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [&loop, reply, &result] {
		auto content = QJsonDocument::fromJson(reply->readAll());
		qDebug().noquote() << content.toJson(QJsonDocument::Indented);

		if (reply->error() == QNetworkReply::NoError) {
			if (content.object()["job"].toObject()["status"] == "done") {
				result = true;
			}
		}

		loop.exit();
	});
	loop.exec();
	return result;
}

bool retrieveJob(const QString &token,
                 const QString &id,
                 bool alignment) {
	bool result = false;
	QUrl url(QString("https://trial.asr.api.speechmatics.com/v2/jobs/%0/%1").arg(id, alignment ? "alignment" : "transcript"));
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("Authorization", QString("Bearer %0").arg(token).toUtf8());
	QNetworkAccessManager manager;
	QEventLoop loop;
	auto reply = manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [&loop, reply, alignment, &result] {
		auto content = alignment ? reply->readAll() : QJsonDocument::fromJson(reply->readAll()).toJson(QJsonDocument::Indented);
		qDebug().noquote() << content;

		if (reply->error() == QNetworkReply::NoError) {
			qDebug() << "good!";
			result = true;
		}

		loop.exit();
	});
	loop.exec();
	return result;
}

int main(int argc,
         char **argv) {
	QCoreApplication app(argc, argv);

	if (argc < 4) {
		qCritical() << "Please provide an api key and the file or url to transcript, the language and an optional script for alignement.";
		return -1;
	}

	QString token = argv[1];
	QString url = argv[2];
	QString language = argv[3];
	QString scriptFileName = argv[4];
	QString id = createJob(token, url, language, scriptFileName);

	if (!id.isEmpty()) {
		while (!checkJob(token, id)) {
			QThread::msleep(5000);
		}

		retrieveJob(token, id, QFile::exists(scriptFileName));
	}

	return 0;
}
