/* -*- Mode: Objective-C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import "mozHTMLAccessible.h"

#import "Accessible-inl.h"
#import "HyperTextAccessible.h"

#import "nsCocoaUtils.h"

@implementation mozHeadingAccessible

- (NSString*)title
{
  nsAutoString title;
  mozilla::ErrorResult rv;
  // XXX use the flattening API when there are available
  // see bug 768298
  mGoannaAccessible->GetContent()->GetTextContent(title, rv);

  return nsCocoaUtils::ToNSString(title);
}

- (id)value
{
  if (!mGoannaAccessible || !mGoannaAccessible->IsHyperText())
    return nil;

  uint32_t level = mGoannaAccessible->AsHyperText()->GetLevelInternal();
  return [NSNumber numberWithInt:level];
}

@end

@interface mozLinkAccessible ()
-(NSURL*)url;
@end

@implementation mozLinkAccessible

- (NSArray*)accessibilityAttributeNames
{
  // if we're expired, we don't support any attributes.
  if (!mGoannaAccessible)
    return [NSArray array];
  
  static NSMutableArray* attributes = nil;
  
  if (!attributes) {
    attributes = [[super accessibilityAttributeNames] mutableCopy];
    [attributes addObject:NSAccessibilityURLAttribute];
  }

  return attributes;
}

- (id)accessibilityAttributeValue:(NSString *)attribute
{
  if ([attribute isEqualToString:NSAccessibilityURLAttribute])
    return [self url];

  return [super accessibilityAttributeValue:attribute];
}

- (NSArray*)accessibilityActionNames 
{
    // if we're expired, we don't support any attributes.
  if (!mGoannaAccessible)
    return [NSArray array];

  static NSArray* actionNames = nil;

  if (!actionNames) {
    actionNames = [[NSArray alloc] initWithObjects:NSAccessibilityPressAction,
                                   nil];
  }

  return actionNames;
}

- (void)accessibilityPerformAction:(NSString*)action 
{
  if (!mGoannaAccessible)
    return;

  if ([action isEqualToString:NSAccessibilityPressAction])
    mGoannaAccessible->DoAction(0);
  else
    [super accessibilityPerformAction:action];
}

- (NSString*)customDescription
{
  return @"";
}

- (NSString*)value
{
  return @"";
}

- (NSURL*)url
{
  if (!mGoannaAccessible || mGoannaAccessible->IsDefunct())
    return nil;

  nsAutoString value;
  mGoannaAccessible->Value(value);

  NSString* urlString = value.IsEmpty() ? nil : nsCocoaUtils::ToNSString(value);
  if (!urlString)
    return nil;

  return [NSURL URLWithString:urlString];
}

@end
