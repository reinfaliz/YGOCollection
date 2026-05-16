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
#include <QKeyEvent> // Added for Keyboard Event Filtering

class YGOCollection : public QWidget {
    Q_OBJECT

public:
    YGOCollection(QWidget *parent = nullptr) : QWidget(parent) {
        setupDatabase();
        setupUI();
        
        networkManager = new QNetworkAccessManager(this);
        connect(networkManager, &QNetworkAccessManager::finished, this, &YGOCollection::onApiReply);
    }

protected:
    // This Event Filter listens for the ENTER key on specific widgets
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (obj == rarityCombo) {
                    quantitySpinBox->setFocus(); // Move selector to quantity
                    return true; 
                } else if (obj == quantitySpinBox) {
                    saveCardToDatabase(); // Trigger the save action
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
    QPushButton *deleteBtn; 
    
    // Data Models
    QSqlTableModel *tableModel;
    QNetworkAccessManager *networkManager;
    
    // Temporary storage
    QString currentCardName;
    QString currentCardNumber;

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
                   "quantity INTEGER)");
    }

    void setupUI() {
        QVBoxLayout *mainLayout = new QVBoxLayout(this);

        // --- Top Row: Import / Export ---
        QHBoxLayout *ioLayout = new QHBoxLayout();
        importBtn = new QPushButton("📥 Import from CSV", this);
        exportBtn = new QPushButton("📤 Export to Excel (CSV)", this);
        ioLayout->addWidget(importBtn);
        ioLayout->addWidget(exportBtn);

        // --- Second Row: Search ---
        QHBoxLayout *searchLayout = new QHBoxLayout();
        cardNumberInput = new QLineEdit(this);
        cardNumberInput->setPlaceholderText("Enter Card Number (e.g., CORI-JP040)");
        cardNumberInput->setStyleSheet("padding: 5px;");
        
        searchBtn = new QPushButton("Search Yugipedia", this);
        searchLayout->addWidget(cardNumberInput);
        searchLayout->addWidget(searchBtn);
        
        // --- Third Row: Input Data ---
        QHBoxLayout *inputLayout = new QHBoxLayout();
        
        rarityCombo = new QComboBox(this);
        rarityCombo->setEnabled(false); 
        rarityCombo->installEventFilter(this); // Install the keyboard listener
        
        quantitySpinBox = new QSpinBox(this);
        quantitySpinBox->setMinimum(1); 
        quantitySpinBox->setEnabled(false);
        quantitySpinBox->installEventFilter(this); // Install the keyboard listener
        
        addBtn = new QPushButton("Add to Collection", this);
        addBtn->setEnabled(false);
        
        inputLayout->addWidget(new QLabel("Rarity:"));
        inputLayout->addWidget(rarityCombo);
        inputLayout->addWidget(new QLabel("Qty:"));
        inputLayout->addWidget(quantitySpinBox);
        inputLayout->addWidget(addBtn);

        statusLabel = new QLabel("Ready.", this);
        statusLabel->setStyleSheet("color: gray;");

        // --- Bottom Row: Database Table ---
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

        // --- New Row: Table Actions ---
        QHBoxLayout *tableActionsLayout = new QHBoxLayout();
        deleteBtn = new QPushButton("❌ Delete Selected", this);
        deleteBtn->setStyleSheet("color: #d9534f; font-weight: bold; padding: 5px;");
        tableActionsLayout->addStretch(); 
        tableActionsLayout->addWidget(deleteBtn);

        // Build Main Layout
        mainLayout->addLayout(ioLayout);
        mainLayout->addLayout(searchLayout);
        mainLayout->addWidget(statusLabel);
        mainLayout->addLayout(inputLayout);
        mainLayout->addWidget(tableView);
        mainLayout->addLayout(tableActionsLayout); 

        // Connect Buttons & Enter Key 
        connect(cardNumberInput, &QLineEdit::returnPressed, this, &YGOCollection::searchCard);
        connect(searchBtn, &QPushButton::clicked, this, &YGOCollection::searchCard);
        connect(addBtn, &QPushButton::clicked, this, &YGOCollection::saveCardToDatabase);
        connect(exportBtn, &QPushButton::clicked, this, &YGOCollection::exportToCsv);
        connect(importBtn, &QPushButton::clicked, this, &YGOCollection::importFromCsv);
        connect(deleteBtn, &QPushButton::clicked, this, &YGOCollection::deleteSelectedCard);

        QShortcut *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), tableView);
        deleteShortcut->setContext(Qt::WidgetShortcut);
        connect(deleteShortcut, &QShortcut::activated, this, &YGOCollection::deleteSelectedCard);
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
        out << "Card Number,Card Name,Rarity,Quantity\n";

        QSqlQuery query("SELECT card_number, card_name, rarity, quantity FROM collection");
        int count = 0;
        while (query.next()) {
            QString num = query.value(0).toString();
            QString name = query.value(1).toString();
            
            if (name.contains(",")) {
                name = "\"" + name + "\"";
            }
            
            QString rar = query.value(2).toString();
            QString qty = query.value(3).toString();
            
            out << num << "," << name << "," << rar << "," << qty << "\n";
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
        query.prepare("INSERT INTO collection (card_number, card_name, rarity, quantity) VALUES (:num, :name, :rarity, :qty)");

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

            if (fields.size() >= 4) {
                query.bindValue(":num", fields[0]);
                query.bindValue(":name", fields[1]);
                query.bindValue(":rarity", fields[2]);
                query.bindValue(":qty", fields[3].toInt());
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
        QString input = cardNumberInput->text().trimmed().toUpper();
        if (input.isEmpty()) return;

        statusLabel->setText("Searching Yugipedia...");
        statusLabel->setStyleSheet("color: blue;");
        searchBtn->setEnabled(false);
        
        QString apiUrl = "https://yugipedia.com/api.php?action=query&format=json&prop=revisions&rvprop=content&rvslots=main&redirects=1&titles=" + input;
        
        QNetworkRequest request((QUrl(apiUrl)));
        request.setHeader(QNetworkRequest::UserAgentHeader, "YgoCollectionManager/1.9 (Contact: user@example.com)");
        
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
            updateQuery.prepare("UPDATE collection SET quantity = :qty WHERE id = :id");
            updateQuery.bindValue(":qty", newQty);
            updateQuery.bindValue(":id", existingId);
            success = updateQuery.exec();
            
            if (success) {
                statusLabel->setText(QString("Added +%1 to existing stack. Total: %2").arg(inputQty).arg(newQty));
            }
        } else {
            QSqlQuery insertQuery;
            insertQuery.prepare("INSERT INTO collection (card_number, card_name, rarity, quantity) "
                                "VALUES (:number, :name, :rarity, :qty)");
            insertQuery.bindValue(":number", currentCardNumber);
            insertQuery.bindValue(":name", currentCardName);
            insertQuery.bindValue(":rarity", inputRarity);
            insertQuery.bindValue(":qty", inputQty);
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
            
            cardNumberInput->setFocus(); // Ensure focus returns to the start
        } else {
            QMessageBox::critical(this, "Database Error", "Could not save card to collection.");
        }
    }

private slots:
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
        QJsonObject queryObj = rootObj["query"].toObject();
        QJsonObject pagesObj = queryObj["pages"].toObject();

        QString pageKey = pagesObj.keys().first();
        
        if (pageKey == "-1") {
            statusLabel->setText("Invalid card number. Not found on Yugipedia.");
            statusLabel->setStyleSheet("color: red;");
            cardNumberInput->selectAll(); // Highlight the bad text so it's easy to overtype
            reply->deleteLater();
            return;
        }

        QJsonObject pageObj = pagesObj[pageKey].toObject();
        currentCardName = pageObj["title"].toString();
        currentCardNumber = cardNumberInput->text().trimmed().toUpper();

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
        
        // --- Smart Focus Logic ---
        if (hasMultipleRarities) {
            rarityCombo->setFocus(); // Give selector to rarity box if they need to choose
        } else {
            quantitySpinBox->setFocus(); // Skip straight to quantity box if there is only 1 choice
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
