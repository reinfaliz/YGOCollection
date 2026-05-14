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
    
    // Temporary storage for the card currently being searched
    QString currentCardName;
    QString currentCardNumber;

    void setupDatabase() {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName("ygo_collection.db");
        
        if (!db.open()) {
            QMessageBox::critical(this, "Database Error", db.lastError().text());
            return;
        }

        // Create the table if it doesn't exist
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
        cardNumberInput->setPlaceholderText("Enter Card Number (e.g., DREV-EN050)");
        cardNumberInput->setStyleSheet("padding: 5px;");
        
        searchBtn = new QPushButton("Search API", this);
        searchLayout->addWidget(cardNumberInput);
        searchLayout->addWidget(searchBtn);
        
        // --- Middle Row: Input Data (Disabled until a valid card is found) ---
        QHBoxLayout *inputLayout = new QHBoxLayout();
        
        rarityCombo = new QComboBox(this);
        rarityCombo->setEnabled(false); // Disabled initially
        
        quantitySpinBox = new QSpinBox(this);
        quantitySpinBox->setMinimum(1); // Must be a positive integer greater than zero
        quantitySpinBox->setEnabled(false);
        
        addBtn = new QPushButton("Add to Collection", this);
        addBtn->setEnabled(false);
        
        inputLayout->addWidget(new QLabel("Rarity:"));
        inputLayout->addWidget(rarityCombo);
        inputLayout->addWidget(new QLabel("Qty:"));
        inputLayout->addWidget(quantitySpinBox);
        inputLayout->addWidget(addBtn);

        // Status Label
        statusLabel = new QLabel("Ready.", this);
        statusLabel->setStyleSheet("color: gray;");

        // --- Bottom Row: Database Table ---
        tableView = new QTableView(this);
        tableModel = new QSqlTableModel(this);
        tableModel->setTable("collection");
        tableModel->select();
        // Allow user to edit quantities/rarities directly in the table
        tableModel->setEditStrategy(QSqlTableModel::OnFieldChange); 
        
        tableView->setModel(tableModel);
        tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        tableView->hideColumn(0); // Hide the ID column

        // Build the main layout
        mainLayout->addLayout(searchLayout);
        mainLayout->addWidget(statusLabel);
        mainLayout->addLayout(inputLayout);
        mainLayout->addWidget(tableView);

        // Connect Buttons
        connect(searchBtn, &QPushButton::clicked, this, &YGOCollection::searchCard);
        connect(addBtn, &QPushButton::clicked, this, &YGOCollection::saveCardToDatabase);
    }

    void searchCard() {
        QString input = cardNumberInput->text().trimmed().toUpper();
        if (input.isEmpty()) return;

        statusLabel->setText("Searching...");
        searchBtn->setEnabled(false);
        
        // Using YGOPRODeck API. Note: To search by a specific set code like DREV-EN050,
        // we can fetch the card sets endpoint or do a general text search. 
        // For this milestone, we will use a generic query and parse the sets.
        QString apiUrl = "https://db.ygoprodeck.com/api/v7/cardinfo.php?cardset=" + input;
        networkManager->get(QNetworkRequest(QUrl(apiUrl)));
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
            tableModel->select(); // Refresh the table view
            
            // Reset UI for next card
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
            statusLabel->setText("Error: Invalid card or network issue.");
            reply->deleteLater();
            return;
        }

        QByteArray response = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
        QJsonObject rootObj = jsonDoc.object();
        
        // If "data" is missing, the card wasn't found
        if (!rootObj.contains("data")) {
            statusLabel->setText("Invalid card number. Not found in database.");
            reply->deleteLater();
            return;
        }

        QJsonArray dataArray = rootObj["data"].toArray();
        QJsonObject firstCard = dataArray[0].toObject();
        
        currentCardName = firstCard["name"].toString();
        currentCardNumber = cardNumberInput->text().trimmed().toUpper();

        // Extract rarities specifically for this set code
        QStringList foundRarities;
        QJsonArray cardSets = firstCard["card_sets"].toArray();
        for (int i = 0; i < cardSets.size(); ++i) {
            QJsonObject setObj = cardSets[i].toObject();
            if (setObj["set_code"].toString() == currentCardNumber) {
                QString rarity = setObj["set_rarity"].toString();
                if (!foundRarities.contains(rarity)) {
                    foundRarities.append(rarity);
                }
            }
        }

        // If the API returned a card but didn't list our specific set code rarity
        // (Sometimes happens with OCG vs TCG differences), fallback to a generic rarity.
        if (foundRarities.isEmpty()) {
            foundRarities.append("Common (Default)");
        }

        // Update the UI based on rarities found
        rarityCombo->clear();
        rarityCombo->addItems(foundRarities);
        
        // If there's only 1 rarity, the combo box handles it automatically, 
        // but we can lock it to prevent user confusion.
        if (foundRarities.size() == 1) {
            rarityCombo->setEnabled(false); 
        } else {
            rarityCombo->setEnabled(true);
        }

        quantitySpinBox->setEnabled(true);
        addBtn->setEnabled(true);
        statusLabel->setText("Card found: " + currentCardName);

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