#include <QString>

#import <UIKit/UIKit.h>

namespace {
UIViewController* topViewController()
{
    UIWindow* window = nil;

    if (@available(iOS 13.0, *)) {
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
            if (scene.activationState != UISceneActivationStateForegroundActive
                || ![scene isKindOfClass:UIWindowScene.class]) {
                continue;
            }

            UIWindowScene* windowScene = (UIWindowScene*)scene;
            for (UIWindow* sceneWindow in windowScene.windows) {
                if (sceneWindow.isKeyWindow) {
                    window = sceneWindow;
                    break;
                }
            }

            if (window) {
                break;
            }
        }
    }

    if (!window) {
        window = UIApplication.sharedApplication.keyWindow;
    }

    UIViewController* controller = window.rootViewController;
    while (controller.presentedViewController) {
        controller = controller.presentedViewController;
    }

    return controller;
}
}

bool DLPresentIosShareSheet(const QString& filePath, QString* error)
{
    __block bool success = false;

    void (^presentShareSheet)(void) = ^{
        UIViewController* controller = topViewController();
        if (!controller) {
            if (error) {
                *error = QStringLiteral("Unable to open the iOS share sheet.");
            }
            return;
        }

        NSString* path = [NSString stringWithUTF8String:filePath.toUtf8().constData()];
        NSURL* fileUrl = [NSURL fileURLWithPath:path];
        UIActivityViewController* activityController =
            [[UIActivityViewController alloc] initWithActivityItems:@[fileUrl]
                                              applicationActivities:nil];

        UIPopoverPresentationController* popover = activityController.popoverPresentationController;
        if (popover) {
            popover.sourceView = controller.view;
            popover.sourceRect = CGRectMake(CGRectGetMidX(controller.view.bounds),
                                            CGRectGetMidY(controller.view.bounds),
                                            1,
                                            1);
            popover.permittedArrowDirections = 0;
        }

        [controller presentViewController:activityController animated:YES completion:nil];
        if (error) {
            error->clear();
        }
        success = true;
    };

    if ([NSThread isMainThread]) {
        presentShareSheet();
    } else {
        dispatch_sync(dispatch_get_main_queue(), presentShareSheet);
    }

    return success;
}
