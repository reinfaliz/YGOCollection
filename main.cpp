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

class YGOCollection : public QWidget {
    Q_OBJECT

public:
    YGOCollection(QWidget *parent = nullptr) : QWidget(parent) {
        setupDatabase();
        setupUI();
        
        networkManager = new QNetworkAccessManager(this);
        connect(networkManager, &QNetworkAccessManager::finished, this, &YGOCollection::onApiReply);
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

        // --- Top Row: Search ---
        QHBoxLayout *searchLayout = new QHBoxLayout();
        cardNumberInput = new QLineEdit(this);
        cardNumberInput->setPlaceholderText("Enter Card Number (e.g., CORI-JP040)");
        cardNumberInput->setStyleSheet("padding: 5px;");
        
        searchBtn = new QPushButton("Search Yugipedia", this);
        searchLayout->addWidget(cardNumberInput);
        searchLayout->addWidget(searchBtn);
        
        // --- Middle Row: Input Data ---
        QHBoxLayout *inputLayout = new QHBoxLayout();
        
        rarityCombo = new QComboBox(this);
        rarityCombo->setEnabled(false); 
        
        quantitySpinBox = new QSpinBox(this);
        quantitySpinBox->setMinimum(1); 
        quantitySpinBox->setEnabled(false);
        
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
        
        tableView->setModel(tableModel);
        tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        tableView->hideColumn(0); 

        mainLayout->addLayout(searchLayout);
        mainLayout->addWidget(statusLabel);
        mainLayout->addLayout(inputLayout);
        mainLayout->addWidget(tableView);

        connect(searchBtn, &QPushButton::clicked, this, &YGOCollection::searchCard);
        connect(addBtn, &QPushButton::clicked, this, &YGOCollection::saveCardToDatabase);
    }

    void searchCard() {
        QString input = cardNumberInput->text().trimmed().toUpper();
        if (input.isEmpty()) return;

        statusLabel->setText("Searching Yugipedia...");
        statusLabel->setStyleSheet("color: blue;");
        searchBtn->setEnabled(false);
        
        QString apiUrl = "https://yugipedia.com/api.php?action=query&format=json&prop=revisions&rvprop=content&rvslots=main&redirects=1&titles=" + input;
        
        QNetworkRequest request((QUrl(apiUrl)));
        request.setHeader(QNetworkRequest::UserAgentHeader, "YgoCollectionManager/1.4 (Contact: user@example.com)");
        
        networkManager->get(request);
    }

    void saveCardToDatabase() {
        QSqlQuery query;
        query.prepare("INSERT INTO collection (card_number, card_name, rarity, quantity) "
                      "VALUES (:number, :name, :rarity, :qty)");
        query.bindValue(":number", currentCardNumber);
        query.bindValue(":name", currentCardName);
        query.bindValue(":rarity", rarityCombo->currentText());
        query.bindValue(":qty", quantitySpinBox->value());

        if (query.exec()) {
            statusLabel->setText("Card added successfully!");
            statusLabel->setStyleSheet("color: green;");
            tableModel->select(); 
            
            cardNumberInput->clear();
            rarityCombo->clear();
            rarityCombo->setEnabled(false);
            quantitySpinBox->setValue(1);
            quantitySpinBox->setEnabled(false);
            addBtn->setEnabled(false);
        } else {
            QMessageBox::critical(this, "Save Error", "Could not save to database.");
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
            reply->deleteLater();
            return;
        }

        QJsonObject pageObj = pagesObj[pageKey].toObject();
        currentCardName = pageObj["title"].toString();
        currentCardNumber = cardNumberInput->text().trimmed().toUpper();

        // 1. Safely extract the wikitext 
        QString wikitext;
        QJsonArray revisions = pageObj["revisions"].toArray();
        if (!revisions.isEmpty()) {
            QJsonObject revObj = revisions[0].toObject();
            if (revObj.contains("slots")) {
                wikitext = revObj["slots"].toObject()["main"].toObject()["*"].toString();
            } else {
                wikitext = revObj["*"].toString(); // Fallback
            }
        }

        // 2. Error Check: Did Qt successfully pull the text?
        if (wikitext.isEmpty()) {
            statusLabel->setText("Error: Wikitext missing from API response.");
            statusLabel->setStyleSheet("color: red;");
            reply->deleteLater();
            return;
        }

        QStringList foundRarities;
        
        // 3. Substring Parser (The Fix)
        int index = 0;
        // Loop through EVERY occurrence of the card number in the wikitext
        while ((index = wikitext.indexOf(currentCardNumber, index, Qt::CaseInsensitive)) != -1) {
            
            // Find the end of this specific line
            int endActual = wikitext.indexOf('\n', index);
            int endLiteral = wikitext.indexOf("\\n", index);
            int endIndex = -1;
            
            if (endActual != -1 && endLiteral != -1) endIndex = qMin(endActual, endLiteral);
            else if (endActual != -1) endIndex = endActual;
            else if (endLiteral != -1) endIndex = endLiteral;
            else endIndex = wikitext.length();
            
            // Extract just the line containing the card number
            QString line = wikitext.mid(index, endIndex - index);
            
            // Look for standard formatting: CORI-JP040; Chaos Origins; Rarities...
            QStringList parts = line.split(';');
            if (parts.size() >= 3) {
                QString raritiesStr = parts[2];
                // Append any extra parts in case the set name had a semicolon in it
                for (int i = 3; i < parts.size(); ++i) {
                    raritiesStr += "," + parts[i]; 
                }

                // Clean out wiki brackets [[ ]], HTML tags <br />, and comments raritiesStr.remove('[').remove(']');
                raritiesStr.remove(QRegularExpression("<[^>]*>"));
                raritiesStr.remove(QRegularExpression(""));
                
                // Split the cleaned string by comma or slash
                QStringList splitRarities = raritiesStr.split(QRegularExpression("[,/]"), Qt::SkipEmptyParts);
                
                for (QString rarity : splitRarities) {
                    rarity = rarity.trimmed();
                    if (!rarity.isEmpty() && !foundRarities.contains(rarity, Qt::CaseInsensitive)) {
                        foundRarities.append(rarity);
                    }
                }
                
                // If we successfully found rarities, break the loop completely
                if (!foundRarities.isEmpty()) {
                    break; 
                }
            }
            
            // If the line didn't have semicolons (e.g. it was an image file link), move to the next occurrence
            index = endIndex; 
        }

        // 4. Update UI
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
        rarityCombo->setEnabled(foundRarities.size() > 1);

        quantitySpinBox->setEnabled(true);
        addBtn->setEnabled(true);
        
        reply->deleteLater();
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    YGOCollection window;
    window.setWindowTitle("Yu-Gi-Oh! Collection Manager");
    window.resize(600, 400);
    window.show();
    
    return app.exec();
}

#include "main.moc"
