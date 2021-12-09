#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

using namespace std;

QString createTranscription(const QString &token,
                            const QString &url) {
	QString id;
	auto multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
	QHttpPart configPart;
	configPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));
	configPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"config\""));
	QJsonObject transcriptionConfigObject;
	transcriptionConfigObject["language"] = "en";
	transcriptionConfigObject["operating_point"] = "enhanced";
	QJsonObject configObject;
	configObject["type"] = "transcription";
	configObject["transcription_config"] = transcriptionConfigObject;

	if (QFile::exists(url)) {
		QFileInfo info(url);
		auto file = new QFile(url);

		if (file->open(QFile::ReadOnly)) {
			QHttpPart mediaPart;
			mediaPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
			qDebug() << info.fileName();
			mediaPart.setHeader(QNetworkRequest::ContentDispositionHeader, QString("attachment; name=\"data_file\"; filename=\"%0\"").arg(info.fileName()));
			mediaPart.setBodyDevice(file);
			file->setParent(multiPart);
			multiPart->append(mediaPart);
		} else {
			qCritical() << "Unable to open" << url;
		}
	} else {
		QJsonObject fetchDataObject;
		fetchDataObject["url"] = url;
		configObject["fetch_data"] = fetchDataObject;
	}

	QJsonDocument body(configObject);
	qDebug().noquote() << body.toJson(QJsonDocument::Indented);
	configPart.setBody(body.toJson());
	multiPart->append(configPart);
	qDebug() << multiPart->boundary();

	QUrl jobUrl("https://trial.asr.api.speechmatics.com/v2/jobs");
	QNetworkRequest request(jobUrl);
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

bool checkTranscription(const QString &token,
                        const QString &id) {
	bool result = false;
	QUrl url(QString("https://trial.asr.api.speechmatics.com/v2/jobs/%0").arg(id));
	QNetworkRequest request(url);
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

bool retrieveTranscription(const QString &token,
                           const QString &id) {
	bool result = false;
	QUrl url(QString("https://trial.asr.api.speechmatics.com/v2/jobs/%0/transcript").arg(id));
	QNetworkRequest request(url);
	request.setRawHeader("Authorization", QString("Bearer %0").arg(token).toUtf8());
	QNetworkAccessManager manager;
	QEventLoop loop;
	auto reply = manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [&loop, reply, &result] {
		auto content = QJsonDocument::fromJson(reply->readAll());
		qDebug().noquote() << content.toJson(QJsonDocument::Indented);

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

	if (argc < 2) {
		qCritical() << "Please provide an api key and the file or url to transcript.";
		return -1;
	}

	QString token = argv[1];
	QString url = argv[2];
	QString id = createTranscription(token, url);

	if (!id.isEmpty()) {
		checkTranscription(token, id);
	}

	return 0;
}
