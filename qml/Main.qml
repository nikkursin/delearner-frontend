import Felgo
import QtQuick
import "Pages"
import "Helper"
import "Components"
import "Views"

/*/////////////////////////////////////
  NOTE:
  Additional integration steps are needed to use Felgo Plugins, for example to add and link required libraries for Android and iOS.
  Please follow the integration steps described in the plugin documentation of your chosen plugins:
  - AdMob: https://felgo.com/doc/plugin-admob/

  To open the documentation of a plugin item in Qt Creator, place your cursor on the item in your QML code and press F1.
  This allows to view the properties, methods and signals of Felgo Plugins directly in Qt Creator.

/////////////////////////////////////*/

App {
    // You get free licenseKeys from https://felgo.com/licenseKey
    // With a licenseKey you can:
    //  * Publish your games & apps for the app stores
    //  * Remove the Felgo Splash Screen or set a custom one (available with the Pro Licenses)
    //  * Add plugins to monetize, analyze & improve your apps (available with the Pro Licenses)
    //licenseKey: "<generate one from https://felgo.com/licenseKey>"

    // This item contains example code for the chosen Felgo Plugins
    // It is hidden by default and will overlay the QML items below if shown


    // PluginMainItem {
    //     id: pluginMainItem
    //     z: 1           // display the plugin example above other items in the QML code below
    //     visible: false // set this to true to show the plugin example
    // }

    // NavigationStack {

    //     DLAppPage {
    //         title: qsTr("Main Page")

    //         navigationBarHidden: true

    //         Image {
    //             source: "../assets/felgo-logo.png"
    //             anchors.centerIn: parent
    //         }
    //     }

    // }

    id: app

       Loader {
           id: screenLoader
           anchors.fill: parent

           sourceComponent: {
               switch (appStateManager.currentScreen) {
               case 0:
                   return startupLoadingPage
               case 1:
                   return wordsPage
               case 2:
                   return wordDetails
               case 3:
                   return addEditWordPage
               case 4:
                   return groupsPage
               case 5:
                   return groupEditPage
               case 6:
                   return quizHomePage
               case 7:
                   return quizSetupPage
               case 8:
                   return translationQuizSessionPage
               case 9:
                   return articleQuizSessionPage
               case 10:
                   return quizResults
               case 11:
                   return settingsPage
               default:
                   return startupLoadingPage
               }
           }
       }

       Component {
           id: startupLoadingPage

           DLStartupLoadingPage {
           }
       }

       Component {
           id: wordsPage

           DLWordsPage {

           }
       }

       Component {
              id: wordDetails

              DLWordDetails {

              }
       }

       Component {
              id: addEditWordPage

              DLAddEditWordPage {

              }
       }

       Component {
              id: groupsPage

              DLGroupsPage {

              }
       }

       Component {
              id: groupEditPage

              DLGroupEditPage {

              }
       }

       Component {
              id: quizHomePage

              DLQuizHomePage {

              }
       }

       Component {
              id: quizSetupPage

              DLQuizSetupPage {

              }
       }

       Component {
              id: translationQuizSessionPage

              DLTranslationQuizSessionPage {

              }
       }

       Component {
              id: articleQuizSessionPage

              DLArticleQuizSessionPage {

              }
       }

       Component {
              id: quizResults

              DLQuizResults {

              }
       }

       Component {
              id: settingsPage

              DLSettingsPage {

              }
       }
}
