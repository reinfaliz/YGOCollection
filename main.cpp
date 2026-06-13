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

// --- NEW: Struct to hold all parsed data cleanly ---
struct CardDetails {
    QString name;
    bool isOfficial;
    QString type;
    QString category;
    QString property;
    QString monsterType;
    QString attribute;
    QString levelRankLink;
    QString pendulumScale;
    QString atk;
    QString def;
};

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
    
    // NEW: Temporary Storage Object
    QString currentCardNumber;
    CardDetails currentCardDetails;

    // --- NEW: Universal Wikitext Parser Engine ---
    CardDetails parseWikitext(const QString& wikitext, const QString& title) {
        CardDetails d;
        d.name = title;
        d.isOfficial = !wikitext.contains("{{Unofficial name|English}}", Qt::CaseInsensitive);

        auto extractValue = [&](const QString& text, const QString& key) -> QString {
            QRegularExpression rx("\\|\\s*" + key + "\\s*=\\s*([^\\n|]+)");
            QRegularExpressionMatch match = rx.match(text);
            if (match.hasMatch()) {
                QString val = match.captured(1).trimmed();
                val.remove('[').remove(']'); // Clean Wiki links
                val.remove(QRegularExpression("<[^>]*>")); // Clean HTML
                val.remove(QRegularExpression("")); // Clean Comments
                return val.trimmed();
            }
            return "";
        };

        d.type = extractValue(wikitext, "card_type");
        d.attribute = extractValue(wikitext, "attribute").toUpper();
        d.atk = extractValue(wikitext, "atk");
        d.def = extractValue(wikitext, "def");
        d.pendulumScale = extractValue(wikitext, "pendulum_scale");
        d.property = extractValue(wikitext, "property");

        // Hierarchy Check for Level / Rank / Link
        QString lvl = extractValue(wikitext, "level");
        QString rnk = extractValue(wikitext, "rank");
        QString lnk = extractValue(wikitext, "link");
        if (!lvl.isEmpty()) d.levelRankLink = lvl;
        else if (!rnk.isEmpty()) d.levelRankLink = rnk;
        else if (!lnk.isEmpty()) d.levelRankLink = lnk;

        // Extracting Monster Types and Category using Priority Filter
        QString typesStr = extractValue(wikitext, "types");
        if (!typesStr.isEmpty()) {
            QStringList typeParts = typesStr.split(QRegularExpression("\\s*/\\s*"));
            QStringList validTypes = {"Aqua", "Beast", "Beast-Warrior", "Creator God", "Cyberse", "Dinosaur", "Divine-Beast", "Dragon", "Fairy", "Fiend", "Fish", "Illusion", "Insect", "Machine", "Plant", "Psychic", "Pyro", "Reptile", "Rock", "Sea Serpent", "Spellcaster", "Thunder", "Warrior", "Winged Beast", "Wyrm", "Zombie"};
            QStringList catPriority = {"Link", "Xyz", "Synchro", "Fusion", "Ritual", "Normal", "Effect"};

            // 1. Find the Base Monster Type
            for (const QString& part : typeParts) {
                QString p = part.trimmed();
                for (const QString& valid : validTypes) {
                    if (p.compare(valid, Qt::CaseInsensitive) == 0) {
                        d.monsterType = valid;
                        break;
                    }
                }
            }

            // 2. Find the Monster Category using strict hierarchy
            for (const QString& priorityCat : catPriority) {
                bool found = false;
                for (const QString& part : typeParts) {
                    if (part.trimmed().compare(priorityCat, Qt::CaseInsensitive) == 0) {
                        d.category = priorityCat;
                        found = true; 
                        break;
                    }
                }
                if (found) break; // Break the outer loop as soon as the highest priority is found!
            }
        }

        // Logic Fallback for missing 'card_type = Monster'
        if (d.type.contains("Spell", Qt::CaseInsensitive)) d.type = "Spell";
        else if (d.type.contains("Trap", Qt::CaseInsensitive)) d.type = "Trap";
        else if (d.type.contains("Token", Qt::CaseInsensitive)) d.type = "Token";
        else {
            if (!d.attribute.isEmpty() || !d.atk.isEmpty() || !d.def.isEmpty() || !d.monsterType.isEmpty()) {
                d.type = "Monster";
            }
        }

        return d;
    }

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
                   
        // Seamless migration: Add the columns safely
        query.exec("ALTER TABLE collection ADD COLUMN is_official INTEGER DEFAULT 1");
        query.exec("ALTER TABLE collection ADD COLUMN card_type TEXT");
        query.exec("ALTER TABLE collection ADD COLUMN monster_category TEXT");
        query.exec("ALTER TABLE collection ADD COLUMN spell_trap_property TEXT");
        query.exec("ALTER TABLE collection ADD COLUMN monster_type TEXT");
        query.exec("ALTER TABLE collection ADD COLUMN attribute TEXT");
        query.exec("ALTER TABLE collection ADD COLUMN level_rank_link TEXT");
        query.exec("ALTER TABLE collection ADD COLUMN pendulum_scale TEXT");
        query.exec("ALTER TABLE collection ADD COLUMN atk TEXT");
        query.exec("ALTER TABLE collection ADD COLUMN def TEXT");
    }

    void setupUI() {
        QVBoxLayout *mainLayout = new QVBoxLayout(this);

        QHBoxLayout *ioLayout = new QHBoxLayout();
        importBtn = new QPushButton("📥 Import from CSV", this);
        exportBtn = new QPushButton("📤 Export to CSV", this);
        syncBtn = new QPushButton("🔄 Update Card Names", this);
        syncBtn->setStyleSheet("background-color: #d1ecf1; font-weight: bold; padding: 5px;");
        
        ioLayout->addWidget(importBtn);
        ioLayout->addWidget(exportBtn);
        ioLayout->addStretch();
        ioLayout->addWidget(syncBtn);

        QHBoxLayout *searchLayout = new QHBoxLayout();
        cardNumberInput = new QLineEdit(this);
        cardNumberInput->setPlaceholderText("Enter Card Number (e.g., DREV-JP002)");
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
        tableModel->setHeaderData(4, Qt::Horizontal, "Qty");
        tableModel->setHeaderData(6, Qt::Horizontal, "Card Type");
        tableModel->setHeaderData(7, Qt::Horizontal, "Monster Category");
        tableModel->setHeaderData(8, Qt::Horizontal, "Spell/Trap Property");
        tableModel->setHeaderData(9, Qt::Horizontal, "Monster Type");
        tableModel->setHeaderData(10, Qt::Horizontal, "Attribute");
        tableModel->setHeaderData(11, Qt::Horizontal, "Level/Rank/Link");
        tableModel->setHeaderData(12, Qt::Horizontal, "Pendulum Scale");
        tableModel->setHeaderData(13, Qt::Horizontal, "ATK");
        tableModel->setHeaderData(14, Qt::Horizontal, "DEF");
        
        tableView->setModel(tableModel);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows); 
        tableView->setSelectionMode(QAbstractItemView::ExtendedSelection); 
        tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        tableView->horizontalHeader()->setStretchLastSection(true); 
        
        tableView->hideColumn(0); // Hide ID
        tableView->hideColumn(5); // Hide is_official
        
        tableView->setColumnWidth(1, 100); 
        tableView->setColumnWidth(2, 200); 

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
                statusLabel->setText(QString("Update Complete! %1 total card(s) officially updated.").arg(totalCardsUpdatedDuringSync));
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
        request.setHeader(QNetworkRequest::UserAgentHeader, "YgoCollectionManager/2.4 (Contact: user@example.com)");
        
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
        out << "Card Number,Card Name,Rarity,Quantity,Is Official,Card Type,Monster Category,Spell/Trap Property,Monster Type,Attribute,Level/Rank/Link,Pendulum Scale,ATK,DEF\n";

        QSqlQuery query("SELECT card_number, card_name, rarity, quantity, is_official, card_type, monster_category, spell_trap_property, monster_type, attribute, level_rank_link, pendulum_scale, atk, def FROM collection");
        int count = 0;
        while (query.next()) {
            QStringList rowData;
            for (int i = 0; i < 14; ++i) {
                QString val = query.value(i).toString();
                if (val.contains(",")) {
                    val = "\"" + val + "\""; // Wrap in quotes if it contains a comma
                }
                rowData.append(val);
            }
            out << rowData.join(",") << "\n";
            count++;
        }
        
        file.close();
        statusLabel->setText(QString("Successfully exported %1 cards to CSV.").arg(count));
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
        
        QSqlQuery checkQuery;
        checkQuery.prepare("SELECT id, quantity FROM collection WHERE card_number = :num AND rarity = :rarity");

        QSqlQuery updateQuery;
        updateQuery.prepare("UPDATE collection SET quantity = :qty, card_name = :name, is_official = :official, "
                            "card_type = :ct, monster_category = :mc, spell_trap_property = :stp, monster_type = :mt, "
                            "attribute = :attr, level_rank_link = :lrl, pendulum_scale = :ps, atk = :atk, def = :def WHERE id = :id");

        QSqlQuery insertQuery;
        insertQuery.prepare("INSERT INTO collection (card_number, card_name, rarity, quantity, is_official, "
                            "card_type, monster_category, spell_trap_property, monster_type, attribute, level_rank_link, pendulum_scale, atk, def) "
                            "VALUES (:num, :name, :rarity, :qty, :official, :ct, :mc, :stp, :mt, :attr, :lrl, :ps, :atk, :def)");

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

            // Strictly expects 14 columns
            if (fields.size() >= 14) {
                QString csvNum = fields[0];
                QString csvName = fields[1];
                QString csvRarity = fields[2];
                int csvQty = fields[3].toInt();
                int csvOfficial = fields[4].toInt();
                
                checkQuery.bindValue(":num", csvNum);
                checkQuery.bindValue(":rarity", csvRarity);
                checkQuery.exec();
                
                if (checkQuery.next()) {
                    int existingId = checkQuery.value(0).toInt();
                    int newQty = checkQuery.value(1).toInt() + csvQty;

                    updateQuery.bindValue(":qty", newQty);
                    updateQuery.bindValue(":name", csvName);
                    updateQuery.bindValue(":official", csvOfficial);
                    updateQuery.bindValue(":ct", fields[5]);
                    updateQuery.bindValue(":mc", fields[6]);
                    updateQuery.bindValue(":stp", fields[7]);
                    updateQuery.bindValue(":mt", fields[8]);
                    updateQuery.bindValue(":attr", fields[9]);
                    updateQuery.bindValue(":lrl", fields[10]);
                    updateQuery.bindValue(":ps", fields[11]);
                    updateQuery.bindValue(":atk", fields[12]);
                    updateQuery.bindValue(":def", fields[13]);
                    updateQuery.bindValue(":id", existingId);
                    updateQuery.exec();
                } else {
                    insertQuery.bindValue(":num", csvNum);
                    insertQuery.bindValue(":name", csvName);
                    insertQuery.bindValue(":rarity", csvRarity);
                    insertQuery.bindValue(":qty", csvQty);
                    insertQuery.bindValue(":official", csvOfficial);
                    insertQuery.bindValue(":ct", fields[5]);
                    insertQuery.bindValue(":mc", fields[6]);
                    insertQuery.bindValue(":stp", fields[7]);
                    insertQuery.bindValue(":mt", fields[8]);
                    insertQuery.bindValue(":attr", fields[9]);
                    insertQuery.bindValue(":lrl", fields[10]);
                    insertQuery.bindValue(":ps", fields[11]);
                    insertQuery.bindValue(":atk", fields[12]);
                    insertQuery.bindValue(":def", fields[13]);
                    insertQuery.exec();
                }
                count++;
            }
        }

        QSqlDatabase::database().commit(); 
        file.close();
        
        tableModel->select(); 
        statusLabel->setText(QString("Successfully imported and merged %1 cards.").arg(count));
        statusLabel->setStyleSheet("color: green;");
    }

    void searchCard() {
        if (!searchBtn->isEnabled()) return; 

        pendingSearchCardNumber = cardNumberInput->text().trimmed().toUpper();
        if (pendingSearchCardNumber.isEmpty()) return;

        QRegularExpression regex("^[A-Z0-9]{2,5}-[A-Z]{0,2}[0-9A-Z]{1,4}$");
        QRegularExpressionMatch match = regex.match(pendingSearchCardNumber);
        
        if (!match.hasMatch()) {
            statusLabel->setText("Invalid format. Please use AAAA-RR### (e.g., DREV-JP002).");
            statusLabel->setStyleSheet("color: red;");
            cardNumberInput->selectAll(); 
            return; 
        }

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
        request.setHeader(QNetworkRequest::UserAgentHeader, "YgoCollectionManager/2.4 (Contact: user@example.com)");
        
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
            updateQuery.prepare("UPDATE collection SET quantity = :qty, card_name = :name, is_official = :official, "
                                "card_type = :ct, monster_category = :mc, spell_trap_property = :stp, monster_type = :mt, "
                                "attribute = :attr, level_rank_link = :lrl, pendulum_scale = :ps, atk = :atk, def = :def WHERE id = :id");
            updateQuery.bindValue(":qty", newQty);
            updateQuery.bindValue(":name", currentCardDetails.name);
            updateQuery.bindValue(":official", currentCardDetails.isOfficial ? 1 : 0);
            updateQuery.bindValue(":ct", currentCardDetails.type);
            updateQuery.bindValue(":mc", currentCardDetails.category);
            updateQuery.bindValue(":stp", currentCardDetails.property);
            updateQuery.bindValue(":mt", currentCardDetails.monsterType);
            updateQuery.bindValue(":attr", currentCardDetails.attribute);
            updateQuery.bindValue(":lrl", currentCardDetails.levelRankLink);
            updateQuery.bindValue(":ps", currentCardDetails.pendulumScale);
            updateQuery.bindValue(":atk", currentCardDetails.atk);
            updateQuery.bindValue(":def", currentCardDetails.def);
            updateQuery.bindValue(":id", existingId);
            success = updateQuery.exec();
            
            if (success) {
                statusLabel->setText(QString("Added +%1 to existing stack. Total: %2").arg(inputQty).arg(newQty));
            }
        } else {
            QSqlQuery insertQuery;
            insertQuery.prepare("INSERT INTO collection (card_number, card_name, rarity, quantity, is_official, "
                                "card_type, monster_category, spell_trap_property, monster_type, attribute, level_rank_link, pendulum_scale, atk, def) "
                                "VALUES (:number, :name, :rarity, :qty, :official, :ct, :mc, :stp, :mt, :attr, :lrl, :ps, :atk, :def)");
            insertQuery.bindValue(":number", currentCardNumber);
            insertQuery.bindValue(":name", currentCardDetails.name);
            insertQuery.bindValue(":rarity", inputRarity);
            insertQuery.bindValue(":qty", inputQty);
            insertQuery.bindValue(":official", currentCardDetails.isOfficial ? 1 : 0);
            insertQuery.bindValue(":ct", currentCardDetails.type);
            insertQuery.bindValue(":mc", currentCardDetails.category);
            insertQuery.bindValue(":stp", currentCardDetails.property);
            insertQuery.bindValue(":mt", currentCardDetails.monsterType);
            insertQuery.bindValue(":attr", currentCardDetails.attribute);
            insertQuery.bindValue(":lrl", currentCardDetails.levelRankLink);
            insertQuery.bindValue(":ps", currentCardDetails.pendulumScale);
            insertQuery.bindValue(":atk", currentCardDetails.atk);
            insertQuery.bindValue(":def", currentCardDetails.def);
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
        updateQuery.prepare("UPDATE collection SET card_name = :name, is_official = 1, "
                            "card_type = :ct, monster_category = :mc, spell_trap_property = :stp, monster_type = :mt, "
                            "attribute = :attr, level_rank_link = :lrl, pendulum_scale = :ps, atk = :atk, def = :def WHERE card_number = :num");

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

            if (!wikitext.isEmpty()) {
                CardDetails d = parseWikitext(wikitext, newTitle);
                
                // If it is now official, forcefully update all its new translated stats in the local database
                if (d.isOfficial) {
                    QString targetNumber = nameToNumberMap.value(newTitle);
                    if (!targetNumber.isEmpty()) {
                        updateQuery.bindValue(":name", d.name);
                        updateQuery.bindValue(":ct", d.type);
                        updateQuery.bindValue(":mc", d.category);
                        updateQuery.bindValue(":stp", d.property);
                        updateQuery.bindValue(":mt", d.monsterType);
                        updateQuery.bindValue(":attr", d.attribute);
                        updateQuery.bindValue(":lrl", d.levelRankLink);
                        updateQuery.bindValue(":ps", d.pendulumScale);
                        updateQuery.bindValue(":atk", d.atk);
                        updateQuery.bindValue(":def", d.def);
                        updateQuery.bindValue(":num", targetNumber);
                        
                        if (updateQuery.exec()) {
                            batchUpdatedCount++;
                        }
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
        QString tempTitle = pageObj["title"].toString();
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
        
        // --- NEW: Parse all the specific data points using the helper function ---
        currentCardDetails = parseWikitext(wikitext, tempTitle);

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
            statusLabel->setText("Card found: " + currentCardDetails.name + " (Rarities not found in text)");
            statusLabel->setStyleSheet("color: orange;");
        } else {
            statusLabel->setText("Card found: " + currentCardDetails.name);
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
    // Increased window size to accommodate 14 columns naturally!
    window.resize(1000, 600);
    window.show();
    
    return app.exec();
}

#include "main.moc"
