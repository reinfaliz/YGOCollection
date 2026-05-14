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
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>

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
        cardNumberInput->setPlaceholderText("Enter Card Number (e.g., DREV-JP050)");
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
        searchBtn->setEnabled(false);
        
        // Yugipedia MediaWiki API endpoint to fetch page content using a set code redirect
        QString apiUrl = "https://yugipedia.com/api.php?action=query&format=json&prop=revisions&rvprop=content&rvslots=main&redirects=1&titles=" + input;
        
        QNetworkRequest request((QUrl(apiUrl)));
        // MediaWiki requires a User-Agent to avoid getting blocked
        request.setHeader(QNetworkRequest::UserAgentHeader, "YgoCollectionManager/1.0 (Contact: user@example.com)");
        
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
        
        // MediaWiki returns a page ID of "-1" if the requested title/redirect doesn't exist
        if (pageKey == "-1") {
            statusLabel->setText("Invalid card number. Not found on Yugipedia.");
            statusLabel->setStyleSheet("color: red;");
            reply->deleteLater();
            return;
        }

        QJsonObject pageObj = pagesObj[pageKey].toObject();
        currentCardName = pageObj["title"].toString();
        currentCardNumber = cardNumberInput->text().trimmed().toUpper();

        // Extract the Wikitext to find the rarity
        QJsonArray revisions = pageObj["revisions"].toArray();
        QString wikitext = revisions[0].toObject()["slots"].toObject()["main"].toObject()["*"].toString();

        QStringList foundRarities;
        
        // Regex to find standard Yu-Gi-Oh! rarities near the specific set code in the raw wikitext
        QString prefix = currentCardNumber.section('-', 0, 0); // e.g., DREV
        QString suffix = currentCardNumber.section('-', 1, 1); // e.g., JP050
        QString pattern = QString("(?:%1[\\-\\|\\s]*%2)[^\\n]{0,40}?(Common|Rare|Super Rare|Ultra Rare|Secret Rare|Ultimate Rare|Ghost Rare|Holographic Rare|Prismatic Secret Rare|Gold Rare|Collector's Rare|Starlight Rare|Quarter Century Secret Rare)").arg(prefix, suffix);
        
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator i = re.globalMatch(wikitext);
        
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString rarity = match.captured(1).trimmed();
            // Capitalize the first letter of each word to keep it clean
            if (!foundRarities.contains(rarity, Qt::CaseInsensitive)) {
                foundRarities.append(rarity);
            }
        }

        if (foundRarities.isEmpty()) {
            foundRarities.append("Common (Default)");
        }

        rarityCombo->clear();
        rarityCombo->addItems(foundRarities);
        rarityCombo->setEnabled(foundRarities.size() > 1);

        quantitySpinBox->setEnabled(true);
        addBtn->setEnabled(true);
        
        statusLabel->setText("Card found: " + currentCardName);
        statusLabel->setStyleSheet("color: black;");

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
