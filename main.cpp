#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QMessageBox>
#include <QTableView>
#include <QHeaderView>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlTableModel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QShortcut>
#include <QKeySequence>
#include <QKeyEvent> 
#include <QTimer>
#include <QDateTime>
#include <QMap>
#include <QList>

class YGOCollection : public QWidget {
    Q_OBJECT

public:
    YGOCollection(QWidget *parent = nullptr) : QWidget(parent) {
        setupDatabase();
        setupUI();
        
        networkManager = new QNetworkAccessManager(this);
        connect(networkManager, &QNetworkAccessManager::finished, this, &YGOCollection::onApiReply);

        syncNetworkManager = new QNetworkAccessManager(this);
        connect(syncNetworkManager, &QNetworkAccessManager::finished, this, &YGOCollection::onSyncApiReply);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (obj == rarityCombo) {
                    quantitySpinBox->setFocus(); 
                    quantitySpinBox->selectAll(); 
                    return true; 
                } else if (obj == quantitySpinBox) {
                    saveCardToDatabase(); 
                    return true; 
                }
            }
        }
        return QWidget::eventFilter(obj, event);
    }

private:
    // UI Elements
    QLineEdit *cardNumberInput;
    QPushButton *searchBtn;
    QComboBox *rarityCombo;
    QSpinBox *quantitySpinBox;
    QPushButton *addBtn;
    QLabel *statusLabel;
    QTableView *tableView;
    
    QPushButton *exportBtn;
    QPushButton *importBtn;
    QPushButton *syncBtn; 
    QPushButton *deleteBtn; 
    
    // Data Models
    QSqlTableModel *tableModel;
    QNetworkAccessManager *networkManager;
    QNetworkAccessManager *syncNetworkManager; 
    
    // API Safety Variables
    qint64 lastApiCallTime = 0;
    QString pendingSearchCardNumber;
    
    // Batch Syncing Variables
    QList<QStringList> pendingSyncBatches; 
    QStringList currentSyncBatch;          
    int totalCardsUpdatedDuringSync = 0;   
    
    // Temporary storage
    QString currentCardName;
    QString currentCardNumber;
    bool currentCardIsOfficial = true; 

    void setupDatabase() {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName("ygo_collection.db");
        
        if (!db.open()) {
            QMessageBox::critical(this, "Database Error", db.lastError().text());
            return;
        }

        QSqlQuery query;
        query.exec("CREATE TABLE IF NOT EXISTS collection ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "card_number TEXT, "
                   "card_name TEXT, "
                   "rarity TEXT, "
                   "quantity INTEGER, "
                   "is_official INTEGER DEFAULT 1)");
                   
        query.exec("ALTER TABLE collection ADD COLUMN is_official INTEGER DEFAULT 1");
    }

    void setupUI() {
        QVBoxLayout *mainLayout = new QVBoxLayout(this);

        QHBoxLayout *ioLayout = new QHBoxLayout();
        importBtn = new QPushButton("📥 Import from CSV", this);
        exportBtn = new QPushButton("📤 Export to CSV", this); // Updated button text
        syncBtn = new QPushButton("🔄 Update Card Names", this);
        syncBtn->setStyleSheet("background-color: #d1ecf1; font-weight: bold; padding: 5px;");
        
        ioLayout->addWidget(importBtn);
        ioLayout->addWidget(exportBtn);
        ioLayout->addStretch();
        ioLayout->addWidget(syncBtn);

        QHBoxLayout *searchLayout = new QHBoxLayout();
        cardNumberInput = new QLineEdit(this);
        cardNumberInput->setPlaceholderText("Enter Card Number (e.g., DREV-JP002)"); // Updated placeholder text
        cardNumberInput->setStyleSheet("padding: 5px;");
        
        searchBtn = new QPushButton("Search Yugipedia", this);
        searchLayout->addWidget(cardNumberInput);
        searchLayout->addWidget(searchBtn);
        
        QHBoxLayout *inputLayout = new QHBoxLayout();
        
        rarityCombo = new QComboBox(this);
        rarityCombo->setEnabled(false); 
        rarityCombo->installEventFilter(this); 
        
        quantitySpinBox = new QSpinBox(this);
        quantitySpinBox->setMinimum(1); 
        quantitySpinBox->setEnabled(false);
        quantitySpinBox->installEventFilter(this); 
        
        addBtn = new QPushButton("Add to Collection", this);
        addBtn->setEnabled(false);
        
        inputLayout->addWidget(new QLabel("Rarity:"));
        inputLayout->addWidget(rarityCombo);
        inputLayout->addWidget(new QLabel("Qty:"));
        inputLayout->addWidget(quantitySpinBox);
        inputLayout->addWidget(addBtn);

        statusLabel = new QLabel("Ready.", this);
        statusLabel->setStyleSheet("color: gray;");

        tableView = new QTableView(this);
        tableModel = new QSqlTableModel(this);
        tableModel->setTable("collection");
        tableModel->select();
        tableModel->setEditStrategy(QSqlTableModel::OnFieldChange); 
        
        tableModel->setHeaderData(1, Qt::Horizontal, "Card Number");
        tableModel->setHeaderData(2, Qt::Horizontal, "Card Name");
        tableModel->setHeaderData(3, Qt::Horizontal, "Rarity");
        tableModel->setHeaderData(4, Qt::Horizontal, "Quantity");
        
        tableView->setModel(tableModel);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows); 
        tableView->setSelectionMode(QAbstractItemView::ExtendedSelection); 
        tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        tableView->horizontalHeader()->setStretchLastSection(true); 
        tableView->setColumnWidth(1, 100); 
        tableView->setColumnWidth(2, 200); 
        
        tableView->hideColumn(0); 
        tableView->hideColumn(5); 

        QHBoxLayout *tableActionsLayout = new QHBoxLayout();
        deleteBtn = new QPushButton("❌ Delete Selected", this);
        deleteBtn->setStyleSheet("color: #d9534f; font-weight: bold; padding: 5px;");
        tableActionsLayout->addStretch(); 
        tableActionsLayout->addWidget(deleteBtn);

        mainLayout->addLayout(ioLayout);
        mainLayout->addLayout(searchLayout);
        mainLayout->addWidget(statusLabel);
        mainLayout->addLayout(inputLayout);
        mainLayout->addWidget(tableView);
        mainLayout->addLayout(tableActionsLayout); 

        connect(cardNumberInput, &QLineEdit::returnPressed, this, &YGOCollection::searchCard);
        connect(searchBtn, &QPushButton::clicked, this, &YGOCollection::searchCard);
        connect(addBtn, &QPushButton::clicked, this, &YGOCollection::saveCardToDatabase);
        connect(exportBtn, &QPushButton::clicked, this, &YGOCollection::exportToCsv);
        connect(importBtn, &QPushButton::clicked, this, &YGOCollection::importFromCsv);
        connect(deleteBtn, &QPushButton::clicked, this, &YGOCollection::deleteSelectedCard);
        connect(syncBtn, &QPushButton::clicked, this, &YGOCollection::startSmartSync);

        QShortcut *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), tableView);
        deleteShortcut->setContext(Qt::WidgetShortcut);
        connect(deleteShortcut, &QShortcut::activated, this, &YGOCollection::deleteSelectedCard);
    }

    void startSmartSync() {
        pendingSyncBatches.clear();
        totalCardsUpdatedDuringSync = 0;
        
        QSqlQuery q("SELECT DISTINCT card_number FROM collection WHERE is_official = 0");
        QStringList batch;
        
        while (q.next()) {
            batch.append(q.value(0).toString());
            if (batch.size() == 50) {
                pendingSyncBatches.append(batch);
                batch.clear();
            }
        }
        if (!batch.isEmpty()) {
            pendingSyncBatches.append(batch); 
        }

        if (pendingSyncBatches.isEmpty()) {
            QMessageBox::information(this, "Update Card Names", "Your database is completely up to date!\n\nNo unofficial card names were found.");
            return;
        }

        syncBtn->setEnabled(false);
        processNextSyncBatch();
    }

    void processNextSyncBatch() {
        if (pendingSyncBatches.isEmpty()) {
            syncBtn->setEnabled(true);
            if (totalCardsUpdatedDuringSync > 0) {
                statusLabel->setText(QString("Update Complete! %1 total card name(s) officially updated.").arg(totalCardsUpdatedDuringSync));
                statusLabel->setStyleSheet("color: green;");
                tableModel->select(); 
            } else {
                statusLabel->setText("Update Complete. Pending cards are still waiting for official release.");
                statusLabel->setStyleSheet("color: black;");
            }
            return;
        }

        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        qint64 timeElapsed = currentTime - lastApiCallTime;

        if (timeElapsed < 1000) {
            int delayRemaining = 1000 - timeElapsed;
            statusLabel->setText(QString("API Cooldown: Waiting %1 ms for next batch... (%2 batches remain)")
                                 .arg(delayRemaining).arg(pendingSyncBatches.size()));
            statusLabel->setStyleSheet("color: orange;");
            
            QTimer::singleShot(delayRemaining, this, &YGOCollection::executeSyncBatch);
        } else {
            executeSyncBatch();
        }
    }

    void executeSyncBatch() {
        lastApiCallTime = QDateTime::currentMSecsSinceEpoch();
        currentSyncBatch = pendingSyncBatches.takeFirst(); 
        
        statusLabel->setText(QString("Syncing batch of %1 cards... (%2 batches remain)").arg(currentSyncBatch.size()).arg(pendingSyncBatches.size()));
        statusLabel->setStyleSheet("color: blue;");

        QString titlesJoined = currentSyncBatch.join("|");
        QString apiUrl = "https://yugipedia.com/api.php?action=query&format=json&prop=revisions&rvprop=content&redirects=1&titles=" + titlesJoined;
        
        QNetworkRequest request((QUrl(apiUrl)));
        request.setHeader(QNetworkRequest::UserAgentHeader, "YgoCollectionManager/2.2 (Contact: user@example.com)");
        
        syncNetworkManager->get(request);
    }

    void deleteSelectedCard() {
        QModelIndexList selectedRows = tableView->selectionModel()->selectedRows();

        if (selectedRows.isEmpty()) {
            QMessageBox::information(this, "No Selection", "Please select at least one card from the table first.");
            return;
        }

        int count = selectedRows.size();
        QString confirmationMessage;

        if (count == 1) {
            int row = selectedRows.first().row();
            QString cardNum = tableModel->data(tableModel->index(row, 1)).toString();
            QString cardName = tableModel->data(tableModel->index(row, 2)).toString();
            QString rarity = tableModel->data(tableModel->index(row, 3)).toString();
            confirmationMessage = QString("Are you sure you want to completely remove '%1 - %2 (%3)' from your collection?")
                                  .arg(cardNum, cardName, rarity);
        } else {
            confirmationMessage = QString("Are you sure you want to completely remove %1 selected cards from your collection?").arg(count);
        }

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Confirm Deletion", confirmationMessage, QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            QSqlDatabase::database().transaction();
            QSqlQuery query;
            query.prepare("DELETE FROM collection WHERE id = :id");

            int deletedCount = 0;
            for (const QModelIndex &index : selectedRows) {
                int cardId = tableModel->data(tableModel->index(index.row(), 0)).toInt();
                query.bindValue(":id", cardId);
                if (query.exec()) {
                    deletedCount++;
                }
            }

            QSqlDatabase::database().commit();

            if (deletedCount > 0) {
                if (count == 1) {
                    statusLabel->setText("Deleted 1 card.");
                } else {
                    statusLabel->setText(QString("Deleted %1 cards.").arg(deletedCount));
                }
                statusLabel->setStyleSheet("color: #d9534f;");
                tableModel->select(); 
            } else {
                QMessageBox::critical(this, "Database Error", "Failed to delete the cards.");
            }
        }
    }

    void exportToCsv() {
        QString fileName = QFileDialog::getSaveFileName(this, "Export Collection", QDir::homePath(), "CSV Files (*.csv)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Export Error", "Cannot write to file.");
            return;
        }

        QTextStream out(&file);
        out << "Card Number,Card Name,Rarity,Quantity,Is Official\n";

        QSqlQuery query("SELECT card_number, card_name, rarity, quantity, is_official FROM collection");
        int count = 0;
        while (query.next()) {
            QString num = query.value(0).toString();
            QString name = query.value(1).toString();
            
            if (name.contains(",")) {
                name = "\"" + name + "\"";
            }
            
            QString rar = query.value(2).toString();
            QString qty = query.value(3).toString();
            QString official = query.value(4).toString(); 
            
            out << num << "," << name << "," << rar << "," << qty << "," << official << "\n";
            count++;
        }
        
        file.close();
        statusLabel->setText(QString("Successfully exported %1 cards to Excel.").arg(count));
        statusLabel->setStyleSheet("color: green;");
    }

    void importFromCsv() {
        QString fileName = QFileDialog::getOpenFileName(this, "Import Collection", QDir::homePath(), "CSV Files (*.csv)");
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Import Error", "Cannot read file.");
            return;
        }

        QTextStream in(&file);
        QString header = in.readLine(); 

        QSqlDatabase::database().transaction(); 
        QSqlQuery query;
        query.prepare("INSERT INTO collection (card_number, card_name, rarity, quantity, is_official) VALUES (:num, :name, :rarity, :qty, :official)");

        int count = 0;
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.trimmed().isEmpty()) continue;

            QStringList fields;
            QString currentField;
            bool inQuotes = false;
            
            for (int i = 0; i < line.length(); ++i) {
                QChar c = line[i];
                if (c == '\"') {
                    inQuotes = !inQuotes; 
                } else if (c == ',' && !inQuotes) {
                    fields.append(currentField.trimmed());
                    currentField.clear(); 
                } else {
                    currentField += c; 
                }
            }
            fields.append(currentField.trimmed());

            if (fields.size() >= 5) {
                query.bindValue(":num", fields[0]);
                query.bindValue(":name", fields[1]);
                query.bindValue(":rarity", fields[2]);
                query.bindValue(":qty", fields[3].toInt());
                query.bindValue(":official", fields[4].toInt());
                
                query.exec();
                count++;
            }
        }

        QSqlDatabase::database().commit(); 
        file.close();
        
        tableModel->select(); 
        statusLabel->setText(QString("Successfully imported %1 cards.").arg(count));
        statusLabel->setStyleSheet("color: green;");
    }

    void searchCard() {
        if (!searchBtn->isEnabled()) return; 

        pendingSearchCardNumber = cardNumberInput->text().trimmed().toUpper();
        if (pendingSearchCardNumber.isEmpty()) return;

        searchBtn->setEnabled(false);

        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        qint64 timeElapsed = currentTime - lastApiCallTime;

        if (timeElapsed < 1000) {
            int delayRemaining = 1000 - timeElapsed;
            statusLabel->setText(QString("API Cooldown: Waiting %1 ms...").arg(delayRemaining));
            statusLabel->setStyleSheet("color: orange;");
            
            QTimer::singleShot(delayRemaining, this, &YGOCollection::executeApiRequest);
        } else {
            executeApiRequest();
        }
    }

    void executeApiRequest() {
        lastApiCallTime = QDateTime::currentMSecsSinceEpoch();
        
        statusLabel->setText("Searching Yugipedia...");
        statusLabel->setStyleSheet("color: blue;");
        
        QString apiUrl = "https://yugipedia.com/api.php?action=query&format=json&prop=revisions&rvprop=content&redirects=1&titles=" + pendingSearchCardNumber + "&maxlag=5";
        
        QNetworkRequest request((QUrl(apiUrl)));
        request.setHeader(QNetworkRequest::UserAgentHeader, "YgoCollectionManager/2.2 (Contact: user@example.com)");
        
        networkManager->get(request);
    }

    void saveCardToDatabase() {
        QString inputRarity = rarityCombo->currentText();
        int inputQty = quantitySpinBox->value();

        QSqlQuery checkQuery;
        checkQuery.prepare("SELECT id, quantity FROM collection WHERE card_number = :num AND rarity = :rarity");
        checkQuery.bindValue(":num", currentCardNumber);
        checkQuery.bindValue(":rarity", inputRarity);
        checkQuery.exec();

        bool success = false;

        if (checkQuery.next()) {
            int existingId = checkQuery.value(0).toInt();
            int newQty = checkQuery.value(1).toInt() + inputQty;

            QSqlQuery updateQuery;
            updateQuery.prepare("UPDATE collection SET quantity = :qty, card_name = :name, is_official = :official WHERE id = :id");
            updateQuery.bindValue(":qty", newQty);
            updateQuery.bindValue(":name", currentCardName);
            updateQuery.bindValue(":official", currentCardIsOfficial ? 1 : 0);
            updateQuery.bindValue(":id", existingId);
            success = updateQuery.exec();
            
            if (success) {
                statusLabel->setText(QString("Added +%1 to existing stack. Total: %2").arg(inputQty).arg(newQty));
            }
        } else {
            QSqlQuery insertQuery;
            insertQuery.prepare("INSERT INTO collection (card_number, card_name, rarity, quantity, is_official) "
                                "VALUES (:number, :name, :rarity, :qty, :official)");
            insertQuery.bindValue(":number", currentCardNumber);
            insertQuery.bindValue(":name", currentCardName);
            insertQuery.bindValue(":rarity", inputRarity);
            insertQuery.bindValue(":qty", inputQty);
            insertQuery.bindValue(":official", currentCardIsOfficial ? 1 : 0);
            success = insertQuery.exec();
            
            if (success) {
                statusLabel->setText("New card added successfully!");
            }
        }

        if (success) {
            statusLabel->setStyleSheet("color: green;");
            tableModel->select(); 
            
            cardNumberInput->clear();
            rarityCombo->clear();
            rarityCombo->setEnabled(false);
            quantitySpinBox->setValue(1);
            quantitySpinBox->setEnabled(false);
            addBtn->setEnabled(false);
            
            cardNumberInput->setFocus(); 
        } else {
            QMessageBox::critical(this, "Database Error", "Could not save card to collection.");
        }
    }

private slots:

    void onSyncApiReply(QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            statusLabel->setText("Update failed: Network Error.");
            statusLabel->setStyleSheet("color: red;");
            syncBtn->setEnabled(true);
            reply->deleteLater();
            return;
        }

        QByteArray response = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
        QJsonObject rootObj = jsonDoc.object();
        QJsonObject queryObj = rootObj["query"].toObject();

        QMap<QString, QString> nameToNumberMap;
        for (const QString& num : currentSyncBatch) {
            nameToNumberMap[num] = num; 
        }
        
        if (queryObj.contains("redirects")) {
            QJsonArray redirects = queryObj["redirects"].toArray();
            for (int i = 0; i < redirects.size(); ++i) {
                QString originalNumber = redirects[i].toObject()["from"].toString();
                QString resolvedName = redirects[i].toObject()["to"].toString();
                nameToNumberMap[resolvedName] = originalNumber;
            }
        }

        QJsonObject pagesObj = queryObj["pages"].toObject();
        int batchUpdatedCount = 0;

        QSqlDatabase::database().transaction();
        QSqlQuery updateQuery;
        updateQuery.prepare("UPDATE collection SET card_name = :name, is_official = 1 WHERE card_number = :num");

        for (const QString& key : pagesObj.keys()) {
            if (key == "-1") continue;

            QJsonObject pageObj = pagesObj[key].toObject();
            QString newTitle = pageObj["title"].toString();
            
            QString wikitext;
            QJsonArray revisions = pageObj["revisions"].toArray();
            if (!revisions.isEmpty()) {
                QJsonObject revObj = revisions[0].toObject();
                if (revObj.contains("slots")) {
                    wikitext = revObj["slots"].toObject()["main"].toObject()["*"].toString();
                } else {
                    wikitext = revObj["*"].toString(); 
                }
            }

            if (!wikitext.isEmpty() && !wikitext.contains("{{Unofficial name|English}}", Qt::CaseInsensitive)) {
                QString targetNumber = nameToNumberMap.value(newTitle);
                if (!targetNumber.isEmpty()) {
                    updateQuery.bindValue(":name", newTitle);
                    updateQuery.bindValue(":num", targetNumber);
                    if (updateQuery.exec()) {
                        batchUpdatedCount++;
                    }
                }
            }
        }
        
        QSqlDatabase::database().commit();
        totalCardsUpdatedDuringSync += batchUpdatedCount; 

        reply->deleteLater();

        processNextSyncBatch();
    }

    void onApiReply(QNetworkReply *reply) {
        searchBtn->setEnabled(true);

        if (reply->error() != QNetworkReply::NoError) {
            statusLabel->setText("Network Error. Could not reach Yugipedia.");
            statusLabel->setStyleSheet("color: red;");
            reply->deleteLater();
            return;
        }

        QByteArray response = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
        QJsonObject rootObj = jsonDoc.object();
        
        if (rootObj.contains("error")) {
            QJsonObject errorObj = rootObj["error"].toObject();
            if (errorObj["code"].toString() == "maxlag") {
                statusLabel->setText("Yugipedia servers are busy (maxlag). Please wait a moment.");
                statusLabel->setStyleSheet("color: red;");
                reply->deleteLater();
                return;
            }
        }
        
        QJsonObject queryObj = rootObj["query"].toObject();
        QJsonObject pagesObj = queryObj["pages"].toObject();

        QStringList pageKeys = pagesObj.keys();
        if (pageKeys.isEmpty()) {
            statusLabel->setText("Error: Unexpected or empty response from API.");
            statusLabel->setStyleSheet("color: red;");
            reply->deleteLater();
            return;
        }

        QString pageKey = pageKeys.first();
        
        if (pageKey == "-1") {
            statusLabel->setText("Invalid card number. Not found on Yugipedia.");
            statusLabel->setStyleSheet("color: red;");
            cardNumberInput->selectAll(); 
            reply->deleteLater();
            return;
        }

        QJsonObject pageObj = pagesObj[pageKey].toObject();
        currentCardName = pageObj["title"].toString();
        currentCardNumber = pendingSearchCardNumber; 

        QString wikitext;
        QJsonArray revisions = pageObj["revisions"].toArray();
        if (!revisions.isEmpty()) {
            QJsonObject revObj = revisions[0].toObject();
            if (revObj.contains("slots")) {
                wikitext = revObj["slots"].toObject()["main"].toObject()["*"].toString();
            } else {
                wikitext = revObj["*"].toString(); 
            }
        }

        if (wikitext.isEmpty()) {
            statusLabel->setText("Error: Wikitext missing from API response.");
            statusLabel->setStyleSheet("color: red;");
            reply->deleteLater();
            return;
        }
        
        currentCardIsOfficial = !wikitext.contains("{{Unofficial name|English}}", Qt::CaseInsensitive);

        QStringList foundRarities;
        int index = 0;
        
        while ((index = wikitext.indexOf(currentCardNumber, index, Qt::CaseInsensitive)) != -1) {
            int endActual = wikitext.indexOf('\n', index);
            int endLiteral = wikitext.indexOf("\\n", index);
            int endIndex = -1;
            
            if (endActual != -1 && endLiteral != -1) endIndex = qMin(endActual, endLiteral);
            else if (endActual != -1) endIndex = endActual;
            else if (endLiteral != -1) endIndex = endLiteral;
            else endIndex = wikitext.length();
            
            QString line = wikitext.mid(index, endIndex - index);
            
            QStringList parts = line.split(';');
            if (parts.size() >= 3) {
                QString raritiesStr = parts[2];
                for (int i = 3; i < parts.size(); ++i) {
                    raritiesStr += "," + parts[i]; 
                }

                raritiesStr.remove('[').remove(']');
                raritiesStr.remove(QRegularExpression("<[^>]*>"));
                raritiesStr.remove(QRegularExpression(""));
                
                QStringList splitRarities = raritiesStr.split(QRegularExpression("[,/]"), Qt::SkipEmptyParts);
                
                for (QString rarity : splitRarities) {
                    rarity = rarity.trimmed();
                    if (!rarity.isEmpty() && !foundRarities.contains(rarity, Qt::CaseInsensitive)) {
                        foundRarities.append(rarity);
                    }
                }
                
                if (!foundRarities.isEmpty()) {
                    break; 
                }
            }
            index = endIndex; 
        }

        if (foundRarities.isEmpty()) {
            foundRarities.append("Common");
            statusLabel->setText("Card found: " + currentCardName + " (Rarities not found in text)");
            statusLabel->setStyleSheet("color: orange;");
        } else {
            statusLabel->setText("Card found: " + currentCardName);
            statusLabel->setStyleSheet("color: black;");
        }

        rarityCombo->clear();
        rarityCombo->addItems(foundRarities);
        
        bool hasMultipleRarities = foundRarities.size() > 1;
        rarityCombo->setEnabled(hasMultipleRarities);
        quantitySpinBox->setEnabled(true);
        addBtn->setEnabled(true);
        
        if (hasMultipleRarities) {
            rarityCombo->setFocus(); 
        } else {
            quantitySpinBox->setFocus(); 
            quantitySpinBox->selectAll(); 
        }
        
        reply->deleteLater();
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    YGOCollection window;
    window.setWindowTitle("Yu-Gi-Oh! Collection Manager");
    window.resize(650, 500);
    window.show();
    
    return app.exec();
}

#include "main.moc"
